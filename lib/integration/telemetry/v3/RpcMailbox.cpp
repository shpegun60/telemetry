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

#include <integration/telemetry/v3/RpcMailbox.hpp>
#include <telemetry/encoded/Dispatcher.hpp>
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

bool validOperation(::telemetry::protocol::rpc::RpcOperation operation) noexcept
{
	switch (operation) {
		case ::telemetry::protocol::rpc::RpcOperation::FieldWrite:
		case ::telemetry::protocol::rpc::RpcOperation::Command:
		case ::telemetry::protocol::rpc::RpcOperation::Service:
		case ::telemetry::protocol::rpc::RpcOperation::FieldRead:
			return true;
	}
	return false;
}

} // namespace

WriteResult RpcMailbox::writeImpl(Cursor cursor, Input input, bool final, AbiTag) noexcept
{
	std::uint32_t transactionId = ::telemetry::protocol::rpc::rpcTransactionId(cursor);
	std::uint32_t offset = ::telemetry::protocol::rpc::rpcOffset(cursor);

	if (cursor == 0) {
		if (input.size() < ::telemetry::protocol::rpc::rpcRequestHeaderBytes ||
		    input[0] != std::byte{'T'} || input[1] != std::byte{'R'} ||
		    input[2] != std::byte{'P'} || input[3] != std::byte{'C'} ||
		    load16(input, 4) != ::telemetry::protocol::rpc::rpcMajor ||
		    load16(input, 6) != ::telemetry::protocol::rpc::rpcMinor || input[21] != std::byte{0} ||
		    load16(input, 22) != 0) {
			return {Status::InvalidData, cursor};
		}
		transactionId = load32(input, 8);
		const auto payloadBytes = load32(input, 16);
		const auto operation = static_cast<::telemetry::protocol::rpc::RpcOperation>(
		    std::to_integer<unsigned>(input[20]));
		if (transactionId == 0 || !validOperation(operation) ||
		    payloadBytes > UINT32_MAX - ::telemetry::protocol::rpc::rpcRequestHeaderBytes ||
		    (operation == ::telemetry::protocol::rpc::RpcOperation::FieldRead &&
		     payloadBytes != 0)) {
			return {Status::InvalidData, cursor};
		}

		// A retry of the retained first fragment must reach byte comparison,
		// rather than being rejected by the monotonic-ID admission rule.
		if (state_ == ::telemetry::protocol::rpc::RpcState::Idle || transactionId != highWaterId_) {
			if (transactionId <= highWaterId_)
				return {Status::CursorExpired, cursor};
			if (state_ == ::telemetry::protocol::rpc::RpcState::Pending ||
			    state_ == ::telemetry::protocol::rpc::RpcState::Running)
				return {Status::InvalidData, cursor};

			const auto total = ::telemetry::protocol::rpc::rpcRequestHeaderBytes + payloadBytes;
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
			state_ = final ? ::telemetry::protocol::rpc::RpcState::Pending
			               : ::telemetry::protocol::rpc::RpcState::Receiving;
			return {Status::Ok,
			        ::telemetry::protocol::rpc::rpcCursor(transactionId, receivedBytes_),
			        receivedBytes_, final};
		}
	} else {
		if (transactionId == 0)
			return {Status::InvalidCursor, cursor};
		if (state_ == ::telemetry::protocol::rpc::RpcState::Idle || transactionId != highWaterId_) {
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
		if (final && state_ == ::telemetry::protocol::rpc::RpcState::Receiving)
			state_ = ::telemetry::protocol::rpc::RpcState::Pending;
		return {Status::Ok, ::telemetry::protocol::rpc::rpcCursor(transactionId, end), count,
		        final};
	}

	if (state_ != ::telemetry::protocol::rpc::RpcState::Receiving)
		return {Status::InvalidData, cursor};
	std::memcpy(request_.data() + offset, input.data(), count);
	receivedBytes_ = end;
	lastProgressMs_ = nowMs_;
	if (final)
		state_ = ::telemetry::protocol::rpc::RpcState::Pending;
	return {Status::Ok, ::telemetry::protocol::rpc::rpcCursor(transactionId, end), count, final};
}

ReadResult RpcMailbox::readImpl(Cursor cursor, Output output, AbiTag) const noexcept
{
	if (cursor == 0) {
		if (output.size() < ::telemetry::protocol::rpc::rpcReplyHeaderBytes)
			return {Status::BufferTooSmall, cursor};

		// Every header byte is assigned; old storage never leaks into a poll.
		output[0] = std::byte{'T'};
		output[1] = std::byte{'R'};
		output[2] = std::byte{'P'};
		output[3] = std::byte{'S'};
		store16(output, 4, ::telemetry::protocol::rpc::rpcMajor);
		store16(output, 6, ::telemetry::protocol::rpc::rpcMinor);
		store32(output, 8, highWaterId_);
		store32(output, 12, endpointId_);
		store32(output, 16, resultBytes_);
		output[20] = static_cast<std::byte>(operation_);
		output[21] = static_cast<std::byte>(state_);
		output[22] = static_cast<std::byte>(dispatchStatus_);
		output[23] = static_cast<std::byte>(endpointStatus_);
		store32(output, 24, receivedBytes_);
		store32(output, 28, requestBytes_);
		if (state_ != ::telemetry::protocol::rpc::RpcState::Ready)
			return {Status::Ok, 0, ::telemetry::protocol::rpc::rpcReplyHeaderBytes, true};

		const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(
		    output.size() - ::telemetry::protocol::rpc::rpcReplyHeaderBytes, resultBytes_));
		if (count != 0)
			std::memcpy(output.data() + ::telemetry::protocol::rpc::rpcReplyHeaderBytes,
			            result_.data(), count);
		return {Status::Ok,
		        ::telemetry::protocol::rpc::rpcCursor(
		            highWaterId_, ::telemetry::protocol::rpc::rpcReplyHeaderBytes + count),
		        ::telemetry::protocol::rpc::rpcReplyHeaderBytes + count, count == resultBytes_};
	}

	const auto transactionId = ::telemetry::protocol::rpc::rpcTransactionId(cursor);
	const auto offset = ::telemetry::protocol::rpc::rpcOffset(cursor);
	if (transactionId == 0)
		return {Status::InvalidCursor, cursor};
	if (state_ == ::telemetry::protocol::rpc::RpcState::Idle || transactionId != highWaterId_) {
		return {transactionId <= highWaterId_ ? Status::CursorExpired : Status::InvalidCursor,
		        cursor};
	}
	if (state_ != ::telemetry::protocol::rpc::RpcState::Ready ||
	    offset < ::telemetry::protocol::rpc::rpcReplyHeaderBytes || offset > size())
		return {Status::InvalidCursor, cursor};
	const auto remaining = size() - offset;
	if (remaining == 0)
		return {Status::Ok, cursor, 0, true};
	if (output.empty())
		return {Status::BufferTooSmall, cursor};
	const auto count = static_cast<std::uint32_t>(std::min(output.size(), std::size_t{remaining}));
	std::memcpy(output.data(),
	            result_.data() + offset - ::telemetry::protocol::rpc::rpcReplyHeaderBytes, count);
	return {Status::Ok, ::telemetry::protocol::rpc::rpcCursor(transactionId, offset + count), count,
	        count == remaining};
}

void RpcMailbox::processImpl(std::uint32_t nowMs, AbiTag) noexcept
{
	if (state_ == ::telemetry::protocol::rpc::RpcState::Running)
		return;
	nowMs_ = nowMs;
	if (state_ == ::telemetry::protocol::rpc::RpcState::Receiving) {
		if (std::uint32_t(nowMs_ - lastProgressMs_) >=
		    ::telemetry::protocol::rpc::rpcReceiveTimeoutMs)
			clearRequest();
		return;
	}
	if (state_ != ::telemetry::protocol::rpc::RpcState::Pending)
		return;

	state_ = ::telemetry::protocol::rpc::RpcState::Running;
	const Input payload{request_.data() + ::telemetry::protocol::rpc::rpcRequestHeaderBytes,
	                    requestBytes_ - ::telemetry::protocol::rpc::rpcRequestHeaderBytes};
	// All four operations use the existing checked boundary. In particular,
	// maximum output and scratch capacity are checked before any callback.
	const auto result =
	    ::telemetry::dispatchEncoded(*model_, static_cast<::telemetry::Operation>(operation_),
	                                 endpointId_, payload, result_, *workspace_);
	dispatchStatus_ = result.dispatch;
	endpointStatus_ = result.endpointStatus;
	resultBytes_ = result.written;
	if (dispatchStatus_ != ::telemetry::DispatchStatus::Ok)
		endpointStatus_ = 0;
	state_ = ::telemetry::protocol::rpc::RpcState::Ready;
}

} // namespace resource::telemetry::v3
