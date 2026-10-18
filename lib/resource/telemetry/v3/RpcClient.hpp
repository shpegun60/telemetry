/*
 * @file RpcClient.hpp
 * @brief Typed, caller-buffered asynchronous RPC v1 client.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * All storage is borrowed, stable and mutually disjoint for this object's
 * lifetime. The application serializes access, pairs each complete Resource
 * reply with its generation/requestId and serializes a shared transport channel.
 * Typed endpoint declarations explicitly trust an application-owned schema;
 * they do not validate a remote descriptor or authenticate a peer.
 */
#ifndef RESOURCE_TELEMETRY_V3_RPC_CLIENT_HPP
#define RESOURCE_TELEMETRY_V3_RPC_CLIENT_HPP
#pragma once

#include "RpcWire.hpp"
#include <telemetry/codec/Codec.hpp>
#include <telemetry/result/Dispatch.hpp>
#include <telemetry/result/EndpointStatus.hpp>
#include <cstdlib>
#include <type_traits>

namespace resource::telemetry::v3 {

struct TrustedRpcMapping {
	explicit constexpr TrustedRpcMapping() noexcept = default;
};

inline constexpr TrustedRpcMapping trustedRpcMapping{};

// Explicit assertions that this session's remote ID has the declared types.
// Public methods: FieldEndpoint()/CommandEndpoint()/ServiceEndpoint(): Bind ID.
template<class T>
struct FieldEndpoint {
	static_assert(!std::is_void_v<T>, "RPC Field value cannot be void");
	static_assert(!std::is_const_v<T> && !std::is_volatile_v<T> && !std::is_reference_v<T>,
	              "RPC endpoint types must be unqualified values");
	std::uint32_t id;

	explicit constexpr FieldEndpoint(std::uint32_t value, TrustedRpcMapping) noexcept : id(value)
	{}
};

template<class Request = void>
struct CommandEndpoint {
	static_assert(!std::is_const_v<Request> && !std::is_volatile_v<Request> &&
	                  !std::is_reference_v<Request>,
	              "RPC endpoint types must be unqualified values");
	static_assert(std::is_void_v<Request> ||
	                  ::telemetry::Type<Request>::kind == ::telemetry::TypeKind::Struct,
	              "RPC Command request must be an aggregate struct or void");
	std::uint32_t id;

	explicit constexpr CommandEndpoint(std::uint32_t value, TrustedRpcMapping) noexcept : id(value)
	{}
};

template<class Request, class Response>
struct ServiceEndpoint {
	static_assert(!std::is_const_v<Request> && !std::is_volatile_v<Request> &&
	                  !std::is_reference_v<Request> && !std::is_const_v<Response> &&
	                  !std::is_volatile_v<Response> && !std::is_reference_v<Response>,
	              "RPC endpoint types must be unqualified values");
	static_assert(std::is_void_v<Request> ||
	                  ::telemetry::Type<Request>::kind == ::telemetry::TypeKind::Struct,
	              "RPC Service request must be an aggregate struct or void");
	static_assert(std::is_void_v<Response> ||
	                  ::telemetry::Type<Response>::kind == ::telemetry::TypeKind::Struct,
	              "RPC Service response must be an aggregate struct or void");
	std::uint32_t id;

	explicit constexpr ServiceEndpoint(std::uint32_t value, TrustedRpcMapping) noexcept : id(value)
	{}
};

enum class RpcClientStatus : std::uint8_t {
	Ok,
	Busy,
	NoSession,
	InvalidGeneration,
	TransactionIdsExhausted,
	RequestIdsExhausted,
	BufferTooSmall,
	Overlap,
	CodecError,
	Timeout,
	SessionLost,
	Cancelled,
	MalformedReply,
	ResourceError,
	WrongType,
	ResultUnavailable,
	OutcomeUnknown
};

enum class RpcClientState : std::uint8_t {
	Idle,
	WriteReady,
	WaitingWrite,
	Polling,
	ReadReady,
	WaitingRead,
	Completed,
	Failed,
	OutcomeUnknown
};

struct RpcClientOptions {
	std::uint32_t ackTimeoutMs = 1000;
	std::uint32_t transactionTimeoutMs = 30000;
	std::uint32_t pollIntervalMs = 10;
	std::uint32_t maxRetries = 3;
};

struct RpcClientPacket {
	Input bytes{};
	std::uint32_t generation = 0;
	std::uint32_t requestId = 0;

	constexpr explicit operator bool() const noexcept
	{
		return !bytes.empty();
	}
};

struct RpcClientDiagnostic {
	RpcClientStatus status = RpcClientStatus::Ok;
	RpcClientStatus cause = RpcClientStatus::Ok;
	resource::Status resourceStatus = resource::Status::Ok;
	::telemetry::DispatchStatus dispatch = ::telemetry::DispatchStatus::Ok;
	RpcOperation operation = static_cast<RpcOperation>(0);
	std::uint8_t endpointStatus = 0;
	::telemetry::CodecStatus codec = ::telemetry::CodecStatus::Ok;
};

struct RpcClientDecodeResult {
	RpcClientStatus status;
	::telemetry::CodecStatus codec;
};

namespace client_detail {
template<class T>
inline unsigned char typeIdentity = 0;

template<class T>
constexpr const void* typeToken() noexcept
{
	return &typeIdentity<T>;
}
} // namespace client_detail

// Public methods:
// - RpcClient(): Borrow request, result payload and stable outgoing storage.
// - startSession(): Admit a fresh generation; no automatic replay across it.
// - begin*(): Encode one typed invocation, preserving prior evidence on failure.
// - process(): Supply modulo-u32 time and advance deadlines/poll scheduling.
// - nextPacket(): Borrow a packet until its paired reply, reset or session loss.
// - onSendAccepted(): Report complete packet handoff; start its ACK deadline.
// - onReply(): Validate a complete reply with external pairing metadata.
// - loseSession()/reset(): End work; reset retains transaction high-water mark.
// - state()/status()/diagnostic()/completed()/active(): Inspect lifecycle.
// - resultBytes()/decodeResult(): Access only a validated complete result.
class RpcClient {
public:
	explicit RpcClient(FileIndex mailboxIndex, Output requestStorage, Output resultStorage,
	                   Output packetStorage, RpcClientOptions options = {}) noexcept;
	RpcClient(const RpcClient&) = delete;
	RpcClient& operator=(const RpcClient&) = delete;
	RpcClient(RpcClient&&) = delete;
	RpcClient& operator=(RpcClient&&) = delete;

	[[nodiscard]] RpcClientStatus startSession(std::uint32_t generation,
	                                           std::uint32_t lastTransactionId = 0) noexcept;
	void process(std::uint32_t nowMs) noexcept;
	[[nodiscard]] RpcClientPacket nextPacket() noexcept;
	[[nodiscard]] bool onSendAccepted(std::uint32_t generation, std::uint32_t requestId) noexcept;
	[[nodiscard]] bool onReply(Input reply, std::uint32_t generation,
	                           std::uint32_t requestId) noexcept;
	void loseSession(std::uint32_t generation) noexcept;
	void reset() noexcept;

	[[nodiscard]] RpcClientState state() const noexcept
	{
		return state_;
	}

	[[nodiscard]] RpcClientStatus status() const noexcept
	{
		return diagnostic_.status;
	}

	[[nodiscard]] const RpcClientDiagnostic& diagnostic() const noexcept
	{
		return diagnostic_;
	}

	[[nodiscard]] bool completed() const noexcept
	{
		return state_ == RpcClientState::Completed;
	}

	[[nodiscard]] bool active() const noexcept;
	[[nodiscard]] Input resultBytes() const noexcept;

	template<class T>
	[[nodiscard]] RpcClientStatus beginReadField(FieldEndpoint<T> endpoint) noexcept
	{
		return beginEmpty<T>(RpcOperation::FieldRead, endpoint.id);
	}

	template<class T>
	[[nodiscard]] RpcClientStatus beginWriteField(FieldEndpoint<T> endpoint,
	                                              const T& value) noexcept
	{
		return beginValue<T, void>(RpcOperation::FieldWrite, endpoint.id, value);
	}

	template<class Q>
	[[nodiscard]] RpcClientStatus beginCommand(CommandEndpoint<Q> endpoint, const Q& value) noexcept
	{
		return beginValue<Q, void>(RpcOperation::Command, endpoint.id, value);
	}

	[[nodiscard]] RpcClientStatus beginCommand(CommandEndpoint<void> endpoint) noexcept
	{
		return beginEmpty<void>(RpcOperation::Command, endpoint.id);
	}

	template<class Q, class R>
	[[nodiscard]] RpcClientStatus beginService(ServiceEndpoint<Q, R> endpoint,
	                                           const Q& value) noexcept
	{
		return beginValue<Q, R>(RpcOperation::Service, endpoint.id, value);
	}

	template<class R>
	[[nodiscard]] RpcClientStatus beginService(ServiceEndpoint<void, R> endpoint) noexcept
	{
		return beginEmpty<R>(RpcOperation::Service, endpoint.id);
	}

	template<class T>
	[[nodiscard]] RpcClientDecodeResult decodeResult(T& output) noexcept
	{
		const auto status = canDecode(client_detail::typeToken<T>());
		if (status != RpcClientStatus::Ok)
			return {status, diagnostic_.codec};
		// Input can borrow only result_; an output elsewhere in this client's
		// storage would destroy request replay or the stable packet contract.
		const auto bytes = std::as_bytes(std::span<T>{std::addressof(output), 1});
		if (overlapsStorage(bytes))
			return {RpcClientStatus::Overlap, ::telemetry::CodecStatus::Overlap};
		diagnostic_.codec = ::telemetry::decode(resultBytes(), output);
		return {diagnostic_.codec == ::telemetry::CodecStatus::Ok ? RpcClientStatus::Ok
		                                                          : RpcClientStatus::CodecError,
		        diagnostic_.codec};
	}

	template<class T>
	[[nodiscard]] RpcClientDecodeResult decodeResult(::telemetry::Workspace::Lease<T>& lease,
	                                                 T*& output) noexcept
	{
		const auto pointerBytes = std::as_bytes(std::span<T*>{std::addressof(output), 1});
		if (overlapsStorage(pointerBytes))
			return {RpcClientStatus::Overlap, ::telemetry::CodecStatus::Overlap};
		output = nullptr;
		const auto status = canDecode(client_detail::typeToken<T>());
		if (status != RpcClientStatus::Ok)
			return {status, diagnostic_.codec};
		if (overlapsStorage(lease.workspaceStorage()))
			return {RpcClientStatus::Overlap, ::telemetry::CodecStatus::Overlap};
		diagnostic_.codec = ::telemetry::decode(resultBytes(), lease, output);
		return {diagnostic_.codec == ::telemetry::CodecStatus::Ok ? RpcClientStatus::Ok
		                                                          : RpcClientStatus::CodecError,
		        diagnostic_.codec};
	}

private:
	template<class R>
	RpcClientStatus beginEmpty(RpcOperation operation, std::uint32_t id) noexcept
	{
		const auto status = preflight(0, ::telemetry::wireSize<R>, {});
		if (status != RpcClientStatus::Ok)
			return status;
		begin(operation, id, 0, ::telemetry::wireSize<R>, client_detail::typeToken<R>());
		return RpcClientStatus::Ok;
	}

	template<class Q, class R>
	RpcClientStatus beginValue(RpcOperation operation, std::uint32_t id, const Q& value) noexcept
	{
		const auto status = preflight(::telemetry::wireSize<Q>, ::telemetry::wireSize<R>,
		                              std::as_bytes(std::span<const Q>{std::addressof(value), 1}));
		if (status != RpcClientStatus::Ok)
			return status;
		const auto encoded = ::telemetry::encode(
		    value, request_.subspan(rpcRequestHeaderBytes, ::telemetry::wireSize<Q>));
		if (encoded != ::telemetry::CodecStatus::Ok)
			return RpcClientStatus::CodecError;
		begin(operation, id, ::telemetry::wireSize<Q>, ::telemetry::wireSize<R>,
		      client_detail::typeToken<R>());
		return RpcClientStatus::Ok;
	}

	[[nodiscard]] RpcClientStatus preflight(std::uint32_t request, std::uint32_t response,
	                                        Input source) const noexcept;
	[[nodiscard]] bool overlapsStorage(Input bytes) const noexcept;
	[[nodiscard]] RpcClientStatus canDecode(const void* token) const noexcept;
	void begin(RpcOperation, std::uint32_t, std::uint32_t, std::uint32_t, const void*) noexcept;
	void fail(RpcClientStatus cause) noexcept;
	void writeReply(Input) noexcept;
	void readReply(Input) noexcept;
	[[nodiscard]] bool buildPacket() noexcept;
	void clearPacket() noexcept;

	Output request_, result_, packet_;
	RpcClientOptions options_;
	FileIndex index_;
	RpcClientDiagnostic diagnostic_{};
	const void* responseType_ = nullptr;
	Cursor cursor_ = 0;
	std::uint32_t generation_ = 0, transactionId_ = 0, ticket_ = 0;
	std::uint32_t requestBytes_ = 0, expectedResultBytes_ = 0, resultBytes_ = 0;
	std::uint32_t offset_ = 0, fragmentBytes_ = 0, packetBytes_ = 0;
	std::uint32_t nowMs_ = 0, startedMs_ = 0, sentMs_ = 0, pollMs_ = 0, attempts_ = 0;
	RpcClientState state_ = RpcClientState::Idle;
	bool session_ = false, offered_ = false, accepted_ = false, writePacket_ = false;
	bool final_ = false, finalAccepted_ = false;
};

} // namespace resource::telemetry::v3
#endif // RESOURCE_TELEMETRY_V3_RPC_CLIENT_HPP
