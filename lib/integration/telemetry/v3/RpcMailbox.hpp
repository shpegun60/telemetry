/*
 * @file RpcMailbox.hpp
 * @brief Bounded optional Resource provider for deferred encoded calls.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * One application-admitted peer owns a mailbox. WRITE copies a request and
 * acknowledges receipt; process() performs at most one encoded dispatch.
 * Full request bytes remain available to validate retries after completion.
 * Transport generation checks, synchronization and descriptor agreement are
 * supplied by the application, not by this provider.
 */
#ifndef RESOURCE_TELEMETRY_V3_RPC_MAILBOX_HPP
#define RESOURCE_TELEMETRY_V3_RPC_MAILBOX_HPP
#pragma once

#include <wire/RpcWire.hpp>
#include <resource/Types.hpp>
#include <telemetry/model/Adapter.hpp>
#include <concepts>
#include <cstdlib>
#include <memory>
#include <type_traits>

namespace resource::telemetry::v3 {

// Borrow stable model, scratch and separate request/result byte storage.
// Buffers and operation spans must obey the disjoint-storage preconditions
// in doc/RpcMailboxV1.md. No lock or target lifetime is supplied here.
// Public methods:
// - RpcMailbox(): Borrow caller storage.
// - size(): Report current record.
// - state(): Inspect dispatch state.
// - read(): Copy status/result bytes.
// - write(): Accept or replay fragment.
// - process(): Tick and dispatch once.
// - resetSession(): Discard quiescent session.
class RpcMailbox {
public:
	// Deduction rejects temporary/braced ModelViews, conversion proxies and
	// temporary scratch even when a caller names this class explicitly.
	template<class Model, class Scratch>
	    requires(std::same_as<std::remove_cvref_t<Model>, ::telemetry::ModelView> &&
	             std::is_lvalue_reference_v<Model &&> &&
	             !std::is_volatile_v<std::remove_reference_t<Model>> &&
	             std::same_as<Scratch, ::telemetry::Workspace&>)
	explicit constexpr RpcMailbox(Model&& model, Scratch&& workspace, Output requestStorage,
	                              Output resultStorage) noexcept
	    : model_(std::addressof(model)), workspace_(std::addressof(workspace)),
	      request_(requestStorage), result_(resultStorage)
	{
		// Invalid fixed capacities are a construction error, never a partially
		// initialized provider. Runtime payload errors use ordinary statuses.
		if (request_.size() < ::telemetry::protocol::rpc::rpcRequestHeaderBytes ||
		    request_.size() > UINT32_MAX ||
		    result_.size() > UINT32_MAX - ::telemetry::protocol::rpc::rpcReplyHeaderBytes) {
			std::abort();
		}
	}

	RpcMailbox(const RpcMailbox&) = delete;
	RpcMailbox& operator=(const RpcMailbox&) = delete;
	RpcMailbox(RpcMailbox&&) = delete;
	RpcMailbox& operator=(RpcMailbox&&) = delete;

	[[nodiscard]] constexpr FileSize size() const noexcept
	{
		return ::telemetry::protocol::rpc::rpcReplyHeaderBytes + resultBytes_;
	}

	[[nodiscard]] constexpr ::telemetry::protocol::rpc::RpcState state() const noexcept
	{
		return state_;
	}

	[[nodiscard]] ReadResult read(Cursor cursor, Output output) const noexcept
	{
		return readImpl(cursor, output, ::telemetry::detail::CurrentStructuredAbiTag{});
	}

	[[nodiscard]] WriteResult write(Cursor cursor, Input input, bool final) noexcept
	{
		return writeImpl(cursor, input, final, ::telemetry::detail::CurrentStructuredAbiTag{});
	}

	// Supply fresh modulo-u32 milliseconds before packets and on periodic
	// ticks. Only accepted new bytes refresh the Receiving deadline.
	void process(std::uint32_t nowMs) noexcept
	{
		processImpl(nowMs, ::telemetry::detail::CurrentStructuredAbiTag{});
	}

	// The owner first excludes the old connection generation and drains work.
	// An active callback cannot be cancelled; reset then becomes a no-op.
	void resetSession() noexcept
	{
		if (state_ == ::telemetry::protocol::rpc::RpcState::Running)
			return;
		clearRequest();
		highWaterId_ = 0;
		nowMs_ = 0;
		lastProgressMs_ = 0;
	}

private:
	using AbiTag = ::telemetry::detail::CurrentStructuredAbiTag;
	[[nodiscard]] ReadResult readImpl(Cursor, Output, AbiTag) const noexcept;
	[[nodiscard]] WriteResult writeImpl(Cursor, Input, bool, AbiTag) noexcept;
	void processImpl(std::uint32_t, AbiTag) noexcept;

	// A timeout drops logical access to bytes, without clearing large buffers.
	// The high-water mark survives; resetSession clears it separately.
	constexpr void clearRequest() noexcept
	{
		state_ = ::telemetry::protocol::rpc::RpcState::Idle;
		operation_ = static_cast<::telemetry::protocol::rpc::RpcOperation>(0);
		endpointId_ = 0;
		requestBytes_ = 0;
		receivedBytes_ = 0;
		resultBytes_ = 0;
		dispatchStatus_ = ::telemetry::DispatchStatus::Ok;
		endpointStatus_ = 0;
	}

	const ::telemetry::ModelView* model_;
	::telemetry::Workspace* workspace_;
	Output request_;
	Output result_;
	std::uint32_t highWaterId_ = 0;
	std::uint32_t endpointId_ = 0;
	std::uint32_t requestBytes_ = 0;
	std::uint32_t receivedBytes_ = 0;
	std::uint32_t resultBytes_ = 0;
	std::uint32_t nowMs_ = 0;
	std::uint32_t lastProgressMs_ = 0;
	::telemetry::protocol::rpc::RpcState state_ = ::telemetry::protocol::rpc::RpcState::Idle;
	::telemetry::protocol::rpc::RpcOperation operation_ =
	    static_cast<::telemetry::protocol::rpc::RpcOperation>(0);
	::telemetry::DispatchStatus dispatchStatus_ = ::telemetry::DispatchStatus::Ok;
	std::uint8_t endpointStatus_ = 0;
};

} // namespace resource::telemetry::v3
#endif // RESOURCE_TELEMETRY_V3_RPC_MAILBOX_HPP
