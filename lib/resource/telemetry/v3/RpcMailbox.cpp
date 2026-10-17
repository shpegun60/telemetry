/*
 * @file RpcMailbox.cpp
 * @brief RPC v1 fragment validation, retained replies and one-shot dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Validation precedes every fragment mutation. The provider retains the
 * complete request to compare retries, while separate result storage lets
 * clients reread a completed call without invoking it again.
 */

#include "RpcMailbox.hpp"
#include <algorithm>
#include <cstring>

namespace resource::telemetry::v3 {
namespace {

// Helpers are used only after validating a complete fixed-size header.
// Explicit byte assembly keeps unaligned packet storage and host byte order
// independent of native object layout.
std::uint16_t load16(Input bytes, std::size_t offset) noexcept
{
	return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[offset]) |
	                                  (std::to_integer<unsigned>(bytes[offset + 1]) << 8));
}

std::uint32_t load32(Input bytes, std::size_t offset) noexcept
{
	std::uint32_t value = 0;
	for (unsigned byte = 0; byte < 4; ++byte)
		value |= std::uint32_t{std::to_integer<unsigned>(bytes[offset + byte])} << (byte * 8);
	return value;
}

void store16(Output bytes, std::size_t offset, std::uint16_t value) noexcept
{
	bytes[offset] = static_cast<std::byte>(value & 0xffu);
	bytes[offset + 1] = static_cast<std::byte>(value >> 8);
}

void store32(Output bytes, std::size_t offset, std::uint32_t value) noexcept
{
	for (unsigned byte = 0; byte < 4; ++byte)
		bytes[offset + byte] = static_cast<std::byte>((value >> (byte * 8)) & 0xffu);
}

bool validOperation(RpcOperation operation) noexcept
{
	switch (operation) {
		case RpcOperation::FieldWrite:
		case RpcOperation::Command:
		case RpcOperation::Service:
		case RpcOperation::FieldRead:
			return true;
	}
	return false;
}

} // namespace

WriteResult RpcMailbox::writeImpl(Cursor cursor, Input input, bool final, AbiTag) noexcept
{
	std::uint32_t transactionId = rpcTransactionId(cursor);
	std::uint32_t offset = rpcOffset(cursor);

	if (cursor == 0) {
		if (input.size() < rpcRequestHeaderBytes || input[0] != std::byte{'T'} ||
		    input[1] != std::byte{'R'} || input[2] != std::byte{'P'} ||
		    input[3] != std::byte{'C'} || load16(input, 4) != rpcMajor ||
		    load16(input, 6) != rpcMinor || input[21] != std::byte{0} || load16(input, 22) != 0) {
			return {Status::InvalidData, cursor};
		}
		transactionId = load32(input, 8);
		const auto payloadBytes = load32(input, 16);
		const auto operation = static_cast<RpcOperation>(std::to_integer<unsigned>(input[20]));
		if (transactionId == 0 || !validOperation(operation) ||
		    payloadBytes > UINT32_MAX - rpcRequestHeaderBytes ||
		    (operation == RpcOperation::FieldRead && payloadBytes != 0)) {
			return {Status::InvalidData, cursor};
		}

		// A retry of the retained first fragment must reach byte comparison,
		// rather than being rejected by the monotonic-ID admission rule.
		if (state_ == RpcState::Idle || transactionId != highWaterId_) {
			if (transactionId <= highWaterId_)
				return {Status::CursorExpired, cursor};
			if (state_ == RpcState::Pending || state_ == RpcState::Running)
				return {Status::InvalidData, cursor};

			const auto total = rpcRequestHeaderBytes + payloadBytes;
			if (input.size() > total || (final && input.size() != total))
				return {Status::InvalidData, cursor};
			if (total > request_.size())
				return {Status::BufferTooSmall, cursor};

			// Only this successful admission invalidates the previous result.
			std::memcpy(request_.data(), input.data(), input.size());
			highWaterId_ = transactionId;
			endpointId_ = load32(input, 12);
			operation_ = operation;
			requestBytes_ = total;
			receivedBytes_ = static_cast<std::uint32_t>(input.size());
			resultBytes_ = 0;
			dispatchStatus_ = ::telemetry::DispatchStatus::Ok;
			endpointStatus_ = 0;
			lastProgressMs_ = nowMs_;
			state_ = final ? RpcState::Pending : RpcState::Receiving;
			return {Status::Ok, rpcCursor(transactionId, receivedBytes_), receivedBytes_, final};
		}
	} else {
		if (transactionId == 0)
			return {Status::InvalidCursor, cursor};
		if (state_ == RpcState::Idle || transactionId != highWaterId_) {
			return {transactionId <= highWaterId_ ? Status::CursorExpired : Status::InvalidCursor,
			        cursor};
		}
	}

	// Check sums through subtraction before narrowing or touching a buffer.
	if (offset > requestBytes_ || input.size() > requestBytes_ - offset)
		return {Status::InvalidData, cursor};
	const auto count = static_cast<std::uint32_t>(input.size());
	const auto end = offset + count;
	if ((final && end != requestBytes_) || (input.empty() && !final))
		return {Status::InvalidData, cursor};
	if (offset > receivedBytes_ || (offset < receivedBytes_ && end > receivedBytes_))
		return {Status::InvalidCursor, cursor};

	if (offset < receivedBytes_ || input.empty()) {
		if (count != 0 && std::memcmp(request_.data() + offset, input.data(), count) != 0)
			return {Status::InvalidData, cursor};
		// A final is a seal instruction. A previously accepted false-final
		// range may seal a complete request, including a separate empty final.
		if (final && state_ == RpcState::Receiving)
			state_ = RpcState::Pending;
		return {Status::Ok, rpcCursor(transactionId, end), count, final};
	}

	if (state_ != RpcState::Receiving)
		return {Status::InvalidData, cursor};
	std::memcpy(request_.data() + offset, input.data(), count);
	receivedBytes_ = end;
	lastProgressMs_ = nowMs_;
	if (final)
		state_ = RpcState::Pending;
	return {Status::Ok, rpcCursor(transactionId, end), count, final};
}

ReadResult RpcMailbox::readImpl(Cursor cursor, Output output, AbiTag) const noexcept
{
	if (cursor == 0) {
		if (output.size() < rpcReplyHeaderBytes)
			return {Status::BufferTooSmall, cursor};

		// Every header byte is assigned; old storage never leaks into a poll.
		output[0] = std::byte{'T'};
		output[1] = std::byte{'R'};
		output[2] = std::byte{'P'};
		output[3] = std::byte{'S'};
		store16(output, 4, rpcMajor);
		store16(output, 6, rpcMinor);
		store32(output, 8, highWaterId_);
		store32(output, 12, endpointId_);
		store32(output, 16, resultBytes_);
		output[20] = static_cast<std::byte>(operation_);
		output[21] = static_cast<std::byte>(state_);
		output[22] = static_cast<std::byte>(dispatchStatus_);
		output[23] = static_cast<std::byte>(endpointStatus_);
		store32(output, 24, receivedBytes_);
		store32(output, 28, requestBytes_);
		if (state_ != RpcState::Ready)
			return {Status::Ok, 0, rpcReplyHeaderBytes, true};

		const auto count = static_cast<std::uint32_t>(
		    std::min<std::size_t>(output.size() - rpcReplyHeaderBytes, resultBytes_));
		if (count != 0)
			std::memcpy(output.data() + rpcReplyHeaderBytes, result_.data(), count);
		return {Status::Ok, rpcCursor(highWaterId_, rpcReplyHeaderBytes + count),
		        rpcReplyHeaderBytes + count, count == resultBytes_};
	}

	const auto transactionId = rpcTransactionId(cursor);
	const auto offset = rpcOffset(cursor);
	if (transactionId == 0)
		return {Status::InvalidCursor, cursor};
	if (state_ == RpcState::Idle || transactionId != highWaterId_) {
		return {transactionId <= highWaterId_ ? Status::CursorExpired : Status::InvalidCursor,
		        cursor};
	}
	if (state_ != RpcState::Ready || offset < rpcReplyHeaderBytes || offset > size())
		return {Status::InvalidCursor, cursor};
	const auto remaining = size() - offset;
	if (remaining == 0)
		return {Status::Ok, cursor, 0, true};
	if (output.empty())
		return {Status::BufferTooSmall, cursor};
	const auto count = static_cast<std::uint32_t>(std::min(output.size(), std::size_t{remaining}));
	std::memcpy(output.data(), result_.data() + offset - rpcReplyHeaderBytes, count);
	return {Status::Ok, rpcCursor(transactionId, offset + count), count, count == remaining};
}

void RpcMailbox::processImpl(std::uint32_t nowMs, AbiTag) noexcept
{
	if (state_ == RpcState::Running)
		return;
	nowMs_ = nowMs;
	if (state_ == RpcState::Receiving) {
		if (std::uint32_t(nowMs_ - lastProgressMs_) >= rpcReceiveTimeoutMs)
			clearRequest();
		return;
	}
	if (state_ != RpcState::Pending)
		return;

	state_ = RpcState::Running;
	const Input payload{request_.data() + rpcRequestHeaderBytes,
	                    requestBytes_ - rpcRequestHeaderBytes};
	// All four operations use the existing checked boundary. In particular,
	// maximum output and scratch capacity are checked before any callback.
	switch (operation_) {
		case RpcOperation::FieldWrite: {
			const auto result =
			    ::telemetry::writeFieldEncoded(*model_, endpointId_, payload, *workspace_);
			dispatchStatus_ = result.dispatch;
			endpointStatus_ = static_cast<std::uint8_t>(result.endpointStatus);
			break;
		}
		case RpcOperation::Command: {
			const auto result =
			    ::telemetry::executeCommandEncoded(*model_, endpointId_, payload, *workspace_);
			dispatchStatus_ = result.dispatch;
			endpointStatus_ = static_cast<std::uint8_t>(result.endpointStatus);
			break;
		}
		case RpcOperation::Service: {
			const auto result = ::telemetry::callServiceEncoded(*model_, endpointId_, payload,
			                                                    result_, *workspace_);
			dispatchStatus_ = result.dispatch;
			endpointStatus_ = static_cast<std::uint8_t>(result.endpointStatus);
			if (result.dispatch == ::telemetry::DispatchStatus::Ok &&
			    result.endpointStatus == ::telemetry::ServiceStatus::Ok)
				resultBytes_ = result.written;
			break;
		}
		case RpcOperation::FieldRead: {
			const auto result =
			    ::telemetry::readFieldEncoded(*model_, endpointId_, result_, *workspace_);
			dispatchStatus_ = result.dispatch;
			if (result.dispatch == ::telemetry::DispatchStatus::Ok)
				resultBytes_ = result.written;
			break;
		}
	}
	if (dispatchStatus_ != ::telemetry::DispatchStatus::Ok)
		endpointStatus_ = 0;
	state_ = RpcState::Ready;
}

} // namespace resource::telemetry::v3
