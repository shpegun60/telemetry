/*
 * @file RpcClient.cpp
 * @brief Bounded RPC packet scheduling, replay and validated result assembly.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "RpcClient.hpp"
#include <resource/protocol/Client.hpp>
#include <algorithm>
#include <cstring>

namespace resource::telemetry::v3 {
namespace {
namespace wireClient = resource::protocol::client;
using wireClient::detail::load;
using wireClient::detail::store;

bool validDispatch(unsigned code) noexcept
{
	return code == 0 || (code >= 4 && code <= 9);
}

bool validEndpoint(RpcOperation operation, unsigned code) noexcept
{
	switch (operation) {
		case RpcOperation::FieldRead:
			return code == 0;
		case RpcOperation::FieldWrite:
			return code <= unsigned(::telemetry::WriteResult::Unavailable);
		case RpcOperation::Command:
			return code <= unsigned(::telemetry::CommandResult::Failed);
		case RpcOperation::Service:
			return code <= unsigned(::telemetry::ServiceStatus::Failed);
	}
	return false;
}
} // namespace

RpcClient::RpcClient(FileIndex mailboxIndex, Output requestStorage, Output resultStorage,
                     Output packetStorage, RpcClientOptions options) noexcept
    : request_(requestStorage), result_(resultStorage), packet_(packetStorage), options_(options),
      index_(mailboxIndex)
{
	if (request_.size() < rpcRequestHeaderBytes || request_.size() > UINT32_MAX ||
	    result_.size() > UINT32_MAX - rpcReplyHeaderBytes ||
	    packet_.size() < resource::protocol::wire::writeRequestHeaderSize + rpcRequestHeaderBytes ||
	    ::telemetry::buffersOverlap(request_, result_) ||
	    ::telemetry::buffersOverlap(request_, packet_) ||
	    ::telemetry::buffersOverlap(result_, packet_) || options_.ackTimeoutMs == 0 ||
	    options_.ackTimeoutMs >= 0x80000000u || options_.transactionTimeoutMs == 0 ||
	    options_.transactionTimeoutMs >= 0x80000000u || options_.pollIntervalMs >= 0x80000000u ||
	    options_.maxRetries == UINT32_MAX)
		std::abort();
}

bool RpcClient::active() const noexcept
{
	return state_ == RpcClientState::WriteReady || state_ == RpcClientState::WaitingWrite ||
	       state_ == RpcClientState::Polling || state_ == RpcClientState::ReadReady ||
	       state_ == RpcClientState::WaitingRead;
}

RpcClientStatus RpcClient::startSession(std::uint32_t generation,
                                        std::uint32_t lastTransactionId) noexcept
{
	if (active())
		return RpcClientStatus::Busy;
	if (generation == 0 || generation <= generation_)
		return RpcClientStatus::InvalidGeneration;
	generation_ = generation;
	transactionId_ = lastTransactionId;
	ticket_ = 0;
	session_ = true;
	reset();
	return RpcClientStatus::Ok;
}

bool RpcClient::overlapsStorage(Input bytes) const noexcept
{
	return ::telemetry::buffersOverlap(bytes, request_) ||
	       ::telemetry::buffersOverlap(bytes, result_) ||
	       ::telemetry::buffersOverlap(bytes, packet_);
}

RpcClientStatus RpcClient::preflight(std::uint32_t request, std::uint32_t response,
                                     Input source) const noexcept
{
	if (active())
		return RpcClientStatus::Busy;
	if (!session_)
		return RpcClientStatus::NoSession;
	if (transactionId_ == UINT32_MAX)
		return RpcClientStatus::TransactionIdsExhausted;
	if (ticket_ == UINT32_MAX)
		return RpcClientStatus::RequestIdsExhausted;
	if (request > UINT32_MAX - rpcRequestHeaderBytes ||
	    request > request_.size() - rpcRequestHeaderBytes || response > result_.size())
		return RpcClientStatus::BufferTooSmall;
	if (overlapsStorage(source))
		return RpcClientStatus::Overlap;
	return RpcClientStatus::Ok;
}

void RpcClient::begin(RpcOperation operation, std::uint32_t id, std::uint32_t payloadBytes,
                      std::uint32_t responseBytes, const void* responseType) noexcept
{
	++transactionId_;
	diagnostic_ = {};
	diagnostic_.operation = operation;
	request_[0] = std::byte{'T'};
	request_[1] = std::byte{'R'};
	request_[2] = std::byte{'P'};
	request_[3] = std::byte{'C'};
	store(request_, 4, rpcMajor);
	store(request_, 6, rpcMinor);
	store(request_, 8, transactionId_);
	store(request_, 12, id);
	store(request_, 16, payloadBytes);
	request_[20] = std::byte(operation);
	request_[21] = request_[22] = request_[23] = std::byte{};
	requestBytes_ = rpcRequestHeaderBytes + payloadBytes;
	expectedResultBytes_ = responseBytes;
	responseType_ = responseType;
	resultBytes_ = offset_ = 0;
	cursor_ = 0;
	startedMs_ = nowMs_;
	finalAccepted_ = false;
	clearPacket();
	state_ = RpcClientState::WriteReady;
}

void RpcClient::clearPacket() noexcept
{
	packetBytes_ = fragmentBytes_ = attempts_ = 0;
	offered_ = accepted_ = final_ = false;
}

void RpcClient::fail(RpcClientStatus cause) noexcept
{
	diagnostic_.cause = cause;
	diagnostic_.status = finalAccepted_ ? RpcClientStatus::OutcomeUnknown : cause;
	state_ = finalAccepted_ ? RpcClientState::OutcomeUnknown : RpcClientState::Failed;
	clearPacket();
	resultBytes_ = 0;
}

void RpcClient::process(std::uint32_t nowMs) noexcept
{
	nowMs_ = nowMs;
	if (!active())
		return;
	if (std::uint32_t(nowMs_ - startedMs_) >= options_.transactionTimeoutMs) {
		fail(RpcClientStatus::Timeout);
		return;
	}
	if ((state_ == RpcClientState::WaitingWrite || state_ == RpcClientState::WaitingRead) &&
	    std::uint32_t(nowMs_ - sentMs_) >= options_.ackTimeoutMs) {
		if (attempts_ > options_.maxRetries) {
			fail(RpcClientStatus::Timeout);
			return;
		}
		// Keep the same bytes, ticket, cursor, final and fragment boundaries.
		offered_ = true;
		state_ = writePacket_ ? RpcClientState::WriteReady : RpcClientState::ReadReady;
	}
	if (state_ == RpcClientState::Polling &&
	    std::uint32_t(nowMs_ - pollMs_) >= options_.pollIntervalMs)
		state_ = RpcClientState::ReadReady;
}

bool RpcClient::buildPacket() noexcept
{
	if (ticket_ == UINT32_MAX) {
		fail(RpcClientStatus::RequestIdsExhausted);
		return false;
	}
	writePacket_ = state_ == RpcClientState::WriteReady;
	wireClient::BuildResult result{};
	if (writePacket_) {
		fragmentBytes_ = static_cast<std::uint32_t>(std::min(
		    {packet_.size() - resource::protocol::wire::writeRequestHeaderSize,
		     resource::protocol::wire::maxPayloadSize, std::size_t(requestBytes_ - offset_)}));
		final_ = fragmentBytes_ == requestBytes_ - offset_;
		result = wireClient::makeWrite(packet_, index_, cursor_,
		                               request_.subspan(offset_, fragmentBytes_), final_);
	} else {
		result = wireClient::makeRead(packet_, index_, cursor_);
	}
	if (result.status != wireClient::BuildStatus::Ok) {
		fail(RpcClientStatus::BufferTooSmall);
		return false;
	}
	++ticket_;
	packetBytes_ = static_cast<std::uint32_t>(result.written);
	offered_ = true;
	return true;
}

RpcClientPacket RpcClient::nextPacket() noexcept
{
	if (state_ != RpcClientState::WriteReady && state_ != RpcClientState::ReadReady)
		return {};
	if (packetBytes_ == 0 && !buildPacket())
		return {};
	return offered_ ? RpcClientPacket{packet_.first(packetBytes_), generation_, ticket_}
	                : RpcClientPacket{};
}

bool RpcClient::onSendAccepted(std::uint32_t generation, std::uint32_t requestId) noexcept
{
	if (!session_ || generation != generation_ || requestId != ticket_ || !offered_ ||
	    packetBytes_ == 0 || !active())
		return false;
	offered_ = false;
	accepted_ = true;
	++attempts_;
	sentMs_ = nowMs_;
	if (writePacket_ && final_)
		finalAccepted_ = true;
	state_ = writePacket_ ? RpcClientState::WaitingWrite : RpcClientState::WaitingRead;
	return true;
}

bool RpcClient::onReply(Input reply, std::uint32_t generation, std::uint32_t requestId) noexcept
{
	if (!session_ || generation != generation_ || requestId != ticket_ || !accepted_ ||
	    packetBytes_ == 0 || !active())
		return false;
	// Reply bytes must not overwrite stable outgoing bytes or retained storage.
	if (overlapsStorage(reply)) {
		fail(RpcClientStatus::Overlap);
		return true;
	}
	if (writePacket_)
		writeReply(reply);
	else
		readReply(reply);
	return true;
}

void RpcClient::writeReply(Input reply) noexcept
{
	const auto parsed = wireClient::parseWrite(reply, fragmentBytes_);
	if (!parsed) {
		fail(RpcClientStatus::MalformedReply);
		return;
	}
	diagnostic_.resourceStatus = parsed.response.status;
	if (parsed.shortError || parsed.response.status != resource::Status::Ok) {
		fail(RpcClientStatus::ResourceError);
		return;
	}
	const auto& ack = parsed.response;
	const auto end = offset_ + fragmentBytes_;
	if (ack.consumed != fragmentBytes_ || ack.next != rpcCursor(transactionId_, end) ||
	    ack.complete != final_) {
		fail(RpcClientStatus::MalformedReply);
		return;
	}
	offset_ = end;
	cursor_ = final_ ? 0 : ack.next;
	state_ = final_ ? RpcClientState::ReadReady : RpcClientState::WriteReady;
	clearPacket();
}

void RpcClient::readReply(Input reply) noexcept
{
	const auto parsed = wireClient::parseRead(reply);
	if (!parsed) {
		fail(RpcClientStatus::MalformedReply);
		return;
	}
	diagnostic_.resourceStatus = parsed.response.status;
	if (parsed.shortError || parsed.response.status != resource::Status::Ok) {
		fail(RpcClientStatus::ResourceError);
		return;
	}
	const auto& chunk = parsed.response;
	auto payload = chunk.data;
	const bool first = cursor_ == 0;
	if (first) {
		if (payload.size() < rpcReplyHeaderBytes ||
		    load<std::uint32_t>(payload, 0) != 0x53505254u ||
		    load<std::uint16_t>(payload, 4) != rpcMajor ||
		    load<std::uint16_t>(payload, 6) != rpcMinor ||
		    load<std::uint32_t>(payload, 8) != transactionId_ ||
		    load<std::uint32_t>(payload, 12) != load<std::uint32_t>(request_, 12) ||
		    payload[20] != std::byte(diagnostic_.operation) ||
		    load<std::uint32_t>(payload, 24) != requestBytes_ ||
		    load<std::uint32_t>(payload, 28) != requestBytes_) {
			fail(RpcClientStatus::MalformedReply);
			return;
		}
		const auto state = static_cast<RpcState>(std::to_integer<unsigned>(payload[21]));
		const auto dispatch = std::to_integer<unsigned>(payload[22]);
		const auto endpoint = std::to_integer<unsigned>(payload[23]);
		const auto size = load<std::uint32_t>(payload, 16);
		if (!validDispatch(dispatch) || !validEndpoint(diagnostic_.operation, endpoint) ||
		    (dispatch != 0 && endpoint != 0)) {
			fail(RpcClientStatus::MalformedReply);
			return;
		}
		if (state == RpcState::Pending || state == RpcState::Running) {
			if (payload.size() != rpcReplyHeaderBytes || size != 0 || dispatch != 0 ||
			    endpoint != 0 || chunk.next != 0 || !chunk.eof) {
				fail(RpcClientStatus::MalformedReply);
				return;
			}
			pollMs_ = nowMs_;
			state_ = RpcClientState::Polling;
			clearPacket();
			return;
		}
		if (state != RpcState::Ready || size > UINT32_MAX - rpcReplyHeaderBytes ||
		    size > result_.size() ||
		    size != ((dispatch == 0 && endpoint == 0) ? expectedResultBytes_ : 0)) {
			fail(RpcClientStatus::MalformedReply);
			return;
		}
		diagnostic_.dispatch = static_cast<::telemetry::DispatchStatus>(dispatch);
		diagnostic_.endpointStatus = static_cast<std::uint8_t>(endpoint);
		resultBytes_ = size;
		offset_ = 0;
		payload = payload.subspan(rpcReplyHeaderBytes);
	}
	const auto remaining = resultBytes_ - offset_;
	if (payload.size() > remaining || (!first && payload.empty() && remaining != 0) ||
	    chunk.next != rpcCursor(transactionId_, rpcReplyHeaderBytes + offset_ +
	                                                static_cast<std::uint32_t>(payload.size())) ||
	    chunk.eof != (payload.size() == remaining)) {
		fail(RpcClientStatus::MalformedReply);
		return;
	}
	if (!payload.empty())
		std::memcpy(result_.data() + offset_, payload.data(), payload.size());
	offset_ += static_cast<std::uint32_t>(payload.size());
	cursor_ = chunk.next;
	state_ = chunk.eof ? RpcClientState::Completed : RpcClientState::ReadReady;
	clearPacket();
}

Input RpcClient::resultBytes() const noexcept
{
	return completed() ? result_.first(resultBytes_) : Input{};
}

RpcClientStatus RpcClient::canDecode(const void* token) const noexcept
{
	if (token != responseType_)
		return RpcClientStatus::WrongType;
	if (!completed() || diagnostic_.dispatch != ::telemetry::DispatchStatus::Ok ||
	    diagnostic_.endpointStatus != 0)
		return RpcClientStatus::ResultUnavailable;
	return RpcClientStatus::Ok;
}

void RpcClient::loseSession(std::uint32_t generation) noexcept
{
	if (generation != generation_ || !session_)
		return;
	session_ = false;
	if (active())
		fail(RpcClientStatus::SessionLost);
}

void RpcClient::reset() noexcept
{
	if (active()) {
		fail(RpcClientStatus::Cancelled);
		return;
	}
	state_ = RpcClientState::Idle;
	diagnostic_ = {};
	responseType_ = nullptr;
	resultBytes_ = 0;
	finalAccepted_ = false;
	clearPacket();
}

} // namespace resource::telemetry::v3
