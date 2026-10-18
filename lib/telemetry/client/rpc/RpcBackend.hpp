/* One-mailbox backend for the portable asynchronous client. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#ifndef TELEMETRY_CLIENT_RPC_BACKEND_HPP
#define TELEMETRY_CLIENT_RPC_BACKEND_HPP
#pragma once
#include "RpcClient.hpp"
#include <telemetry/client/Backend.hpp>
#include <resource/protocol/Wire.hpp>

namespace telemetry::client::rpc {
// Exclusively borrows the native engine and its stable, disjoint buffers.
// Every call uses one owner. The port copies outgoing bytes before handoff.
// A reused native client must have its previous remote session closed first;
// retained non-idle state creates a conservative recovery gate with generation 0.
class RpcBackend final : public Backend {
public:
	explicit RpcBackend(RpcClient& client, Clock clock) noexcept : client_(client), clock_(clock)
	{
		if (!clock_.read)
			std::abort();
		if (client_.state() != RpcClientState::Idle) {
			if (client_.active())
				client_.reset();
			recovery_ = true;
		}
	}

	~RpcBackend() override
	{
		close();
	}

	RpcBackend(const RpcBackend&) = delete;
	RpcBackend& operator=(const RpcBackend&) = delete;
	RpcBackend(RpcBackend&&) = delete;
	RpcBackend& operator=(RpcBackend&&) = delete;

	[[nodiscard]] Status start(RequestView request, std::uint32_t now) noexcept override
	{
		if (recovery_)
			return Status::RecoveryRequired;
		if (!opened_)
			return Status::NotAdmitted;
		if (active_ || terminal_)
			return Status::Busy;
		if (request.timeoutMs == 0 || request.timeoutMs > maxTimeoutMs)
			return Status::InvalidArgument;
		client_.process(now);
		const auto status =
		    client_.beginEncoded(static_cast<RpcOperation>(request.operation), request.endpointId,
		                         request.payload, request.responseBytes);
		if (status != RpcClientStatus::Ok)
			return translate(status);
		operation_ = request.operation;
		deadline_ = now + request.timeoutMs;
		active_ = true;
		finalPrepared_ = false;
		preparedRequest_ = 0;
		outcome_ = {};
		return Status::Ok;
	}

	void process(std::uint32_t now) noexcept override
	{
		if (!active_ || recovery_)
			return;
		if (expired(now, deadline_)) {
			cancel(Status::Timeout);
			return;
		}
		client_.process(now);
		settle();
	}

	[[nodiscard]] bool completed() const noexcept override
	{
		return terminal_;
	}

	[[nodiscard]] ResultView result() const noexcept override
	{
		return terminal_ ? outcome_ : ResultView{Status::Busy};
	}

	void releaseResult() noexcept override
	{
		if (!terminal_)
			return;
		terminal_ = false;
		outcome_ = {};
	}

	void cancel(Status reason) noexcept override
	{
		if (!active_)
			return;
		client_.reset();
		settle(reason == Status::Ok ? Status::Cancelled : reason);
	}

	[[nodiscard]] bool ready() const noexcept override
	{
		return opened_ && !recovery_;
	}

	[[nodiscard]] bool recoveryRequired() const noexcept override
	{
		return recovery_;
	}

	[[nodiscard]] std::uint32_t session() const noexcept override
	{
		return session_;
	}

	[[nodiscard]] std::uint32_t recoveryGeneration() const noexcept
	{
		return session_;
	}

	[[nodiscard]] bool openSession(std::uint32_t generation) noexcept override
	{
		if (recovery_ || generation == 0 || active_ || terminal_)
			return false;
		if (opened_) {
			if (generation == session_)
				return true;
			close();
			return false;
		}
		if (client_.startSession(generation) != RpcClientStatus::Ok)
			return false;
		session_ = generation;
		opened_ = true;
		return true;
	}

	// The application has excluded old frames, drained TX copies and closed the
	// remote session (pool release/drain then fresh acquire). This never admits
	// a schema, replays work or claims an old effect was undone.
	[[nodiscard]] bool confirmSessionClosed(std::uint32_t generation) noexcept override
	{
		if (!recovery_ || generation != session_ || active_ || client_.active())
			return false;
		recovery_ = false;
		return true;
	}

	void close() noexcept override
	{
		if (active_)
			cancel(Status::SessionLost);
		if (opened_)
			requireRecovery();
	}

	[[nodiscard]] Packet nextPacket() noexcept override
	{
		process(clock_.now());
		if (!ready() || !active_)
			return {};
		const auto packet = client_.nextPacket();
		settle();
		return packet
		           ? Packet{packet.bytes, packet.generation, packet.requestId, finalWrite(packet)}
		           : Packet{};
	}

	[[nodiscard]] bool prepareHandoff(std::uint32_t generation,
	                                  std::uint32_t requestId) noexcept override
	{
		process(clock_.now());
		if (!ready() || !active_)
			return false;
		const auto packet = client_.nextPacket();
		if (!packet || packet.generation != generation || packet.requestId != requestId)
			return false;
		preparedRequest_ = requestId;
		if (finalWrite(packet))
			finalPrepared_ = true;
		return true;
	}

	[[nodiscard]] bool onSendAccepted(std::uint32_t generation,
	                                  std::uint32_t requestId) noexcept override
	{
		if (!ready() || !active_)
			return false;
		const auto packet = client_.nextPacket();
		if (!packet || packet.generation != generation || packet.requestId != requestId)
			return false;
		const bool missingGate = preparedRequest_ != requestId;
		// The transfer has happened. Preserve that evidence before reporting a
		// missing pre-handoff gate; a post-send failure cannot undo the transfer.
		if (!client_.onSendAccepted(generation, requestId))
			return false;
		// Each physical send consumes its gate, including an identical ACK-loss retry.
		preparedRequest_ = 0;
		if (finalWrite(packet))
			finalPrepared_ = true;
		if (missingGate) {
			client_.loseSession(generation);
			settle(Status::MalformedReply);
			requireRecovery();
			return false;
		}
		process(clock_.now());
		return true;
	}

	[[nodiscard]] bool onReply(Input bytes, std::uint32_t generation,
	                           std::uint32_t requestId) noexcept override
	{
		process(clock_.now());
		if (!ready() || !active_)
			return false;
		const bool consumed = client_.onReply(bytes, generation, requestId);
		settle();
		return consumed;
	}

private:
	static bool finalWrite(const RpcClientPacket& packet) noexcept
	{
		return packet.bytes.size() >= ::resource::protocol::wire::writeRequestHeaderSize &&
		       packet.bytes[0] == std::byte(::resource::protocol::Op::Write) &&
		       packet.bytes[13] == std::byte{1};
	}

	static Status translate(RpcClientStatus status) noexcept
	{
		switch (status) {
			case RpcClientStatus::Ok:
				return Status::Ok;
			case RpcClientStatus::Busy:
				return Status::Busy;
			case RpcClientStatus::NoSession:
				return Status::NotAdmitted;
			case RpcClientStatus::TransactionIdsExhausted:
			case RpcClientStatus::RequestIdsExhausted:
				return Status::IdsExhausted;
			case RpcClientStatus::BufferTooSmall:
				return Status::BufferTooSmall;
			case RpcClientStatus::CodecError:
				return Status::CodecError;
			case RpcClientStatus::Timeout:
				return Status::Timeout;
			case RpcClientStatus::SessionLost:
				return Status::SessionLost;
			case RpcClientStatus::Cancelled:
				return Status::Cancelled;
			case RpcClientStatus::MalformedReply:
				return Status::MalformedReply;
			case RpcClientStatus::OutcomeUnknown:
				return Status::OutcomeUnknown;
			case RpcClientStatus::ResourceError:
				return Status::TransportError;
			default:
				return Status::InvalidArgument;
		}
	}

	void settle(Status reason = Status::Ok) noexcept
	{
		if (!active_ || client_.active())
			return;
		const auto& diagnostic = client_.diagnostic();
		outcome_ = {Status::Ok,
		            operation_,
		            static_cast<std::uint8_t>(diagnostic.dispatch),
		            diagnostic.endpointStatus,
		            static_cast<std::uint8_t>(diagnostic.codec),
		            {}};
		if (client_.completed()) {
			outcome_.payload = client_.resultBytes();
		} else {
			const bool uncertain =
			    finalPrepared_ || diagnostic.status == RpcClientStatus::OutcomeUnknown;
			const auto cause = diagnostic.status == RpcClientStatus::OutcomeUnknown
			                       ? diagnostic.cause
			                       : diagnostic.status;
			outcome_.status = reason == Status::Ok ? translate(cause) : reason;
			if (uncertain && operation_ != Operation::FieldRead)
				outcome_.status = Status::OutcomeUnknown;
			if (uncertain)
				requireRecovery();
		}
		active_ = false;
		terminal_ = true;
	}

	void requireRecovery() noexcept
	{
		if (recovery_)
			return;
		recovery_ = true;
		opened_ = false;
		if (session_ != 0)
			client_.loseSession(session_);
	}

	RpcClient& client_;
	Clock clock_;
	ResultView outcome_{};
	Operation operation_ = Operation::FieldRead;
	std::uint32_t session_ = 0, preparedRequest_ = 0, deadline_ = 0;
	bool opened_ = false, recovery_ = false, active_ = false, terminal_ = false;
	bool finalPrepared_ = false;
};
} // namespace telemetry::client::rpc
#endif // TELEMETRY_CLIENT_RPC_BACKEND_HPP
