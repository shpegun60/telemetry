/*
 * @file RpcSessionPool.hpp
 * @brief Static session ownership and ACK-gated serialized mailbox dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Bind stable, independently backed mailboxes to application-admitted connection
 * identities. One external owner serializes ingress, successful reply handoffs,
 * release and bounded dispatch. No connection, queue, clock or lock is created.
 */
#ifndef RESOURCE_TELEMETRY_V3_RPC_SESSION_POOL_HPP
#define RESOURCE_TELEMETRY_V3_RPC_SESSION_POOL_HPP
#pragma once

#include "RpcMailbox.hpp"
#include <resource/protocol/Client.hpp>
#include <resource/protocol/Protocol.hpp>
#include <array>
#include <limits>

namespace resource::telemetry::v3 {

// Supplied from trusted connection context, never from the RPC payload. A
// multiplexed transport gives each independently correlated channel its own key.
struct RpcConnectionKey {
	std::uint32_t peer = 0;
	std::uint32_t generation = 0;
	std::uint32_t channel = 0;
	constexpr bool operator==(const RpcConnectionKey&) const noexcept = default;
};

// rpcFile must name this exact mailbox in files. Other providers must not
// forward writes to this or another pool mailbox. All borrowed objects, tables,
// paths, models, workspaces and byte storage keep stable addresses and outlive
// the pool. Exclusive mailbox lifecycle/packet access belongs to this pool.
struct RpcSessionBinding {
	constexpr RpcSessionBinding(RpcMailbox& provider, FileSystemView table,
	                            FileIndex index = 0) noexcept
	    : mailbox(std::addressof(provider)), files(table), rpcFile(index)
	{}

	RpcMailbox* mailbox;
	FileSystemView files;
	FileIndex rpcFile = 0;
};

struct RpcSessionHandle {
	std::size_t index = std::numeric_limits<std::size_t>::max();
	std::uint32_t generation = 0;
	RpcConnectionKey connection{};
	const void* owner = nullptr;
	constexpr bool operator==(const RpcSessionHandle&) const noexcept = default;
};

enum class RpcSessionStatus : std::uint8_t {
	Ok,
	Existing,
	NoCapacity,
	InvalidSession,
	Closing,
	StaleReply,
	Reentrant,
	Exhausted
};

struct RpcSessionAcquireResult {
	RpcSessionStatus status;
	RpcSessionHandle session{};
};

// Local transport bookkeeping only. A ticket is issued solely for a successful
// final WRITE reply to a Pending mailbox; it is never serialized on the wire.
struct RpcReplyTicket {
	const void* owner = nullptr;
	std::size_t index = std::numeric_limits<std::size_t>::max();
	std::uint32_t generation = 0;
	std::uint32_t serial = 0;
	constexpr bool operator==(const RpcReplyTicket&) const noexcept = default;
};

struct RpcSessionHandleResult {
	RpcSessionStatus status;
	protocol::Reply reply{Status::InvalidData, 0};
	RpcReplyTicket ticket{};
};

struct RpcSessionProcessResult {
	RpcSessionStatus status;
	std::size_t dispatched = 0;
	std::size_t visited = 0;
};

// Session handles are local capabilities, not authentication tokens. The same
// active connection key acquires its existing handle, without resetting it.
// Generation exhaustion permanently retires a slot instead of reusing an epoch.
// Public methods:
// - RpcSessionPool(): Copy fixed bindings that borrow stable provider storage.
// - acquire(): Admit an identity without evicting another owner.
// - valid(): Check pool, slot, epoch, identity and admission.
// - release(): Invalidate admission and reset after any running callback returns.
// - handle(): Process one Resource packet without dispatching a mailbox callback.
// - replyAccepted(): Open the dispatch gate after the matching reply was handed off.
// - process(): Tick all slots and dispatch a bounded round-robin callback budget.
template<std::size_t N>
class RpcSessionPool {
	static_assert(N > 0, "RpcSessionPool requires at least one slot");

public:
	explicit RpcSessionPool(std::array<RpcSessionBinding, N> bindings) noexcept
	    : bindings_(bindings), slots_{}
	{
		for (std::size_t i = 0; i < N; ++i) {
			const auto& binding = bindings_[i];
			if (!binding.mailbox || !binding.files[binding.rpcFile].readable() ||
			    !binding.files[binding.rpcFile].writable() ||
			    binding.mailbox->state() == RpcState::Running)
				std::abort();
			for (std::size_t j = 0; j < i; ++j)
				if (bindings_[j].mailbox == binding.mailbox)
					std::abort();
			binding.mailbox->resetSession();
		}
	}

	RpcSessionPool(const RpcSessionPool&) = delete;
	RpcSessionPool& operator=(const RpcSessionPool&) = delete;
	RpcSessionPool(RpcSessionPool&&) = delete;
	RpcSessionPool& operator=(RpcSessionPool&&) = delete;

	[[nodiscard]] RpcSessionAcquireResult acquire(RpcConnectionKey connection) noexcept
	{
		if (processing_ || handling_)
			return {RpcSessionStatus::Reentrant};
		for (std::size_t i = 0; i < N; ++i)
			if (slots_[i].state == SlotState::Active && slots_[i].connection == connection)
				return {RpcSessionStatus::Existing, session(i)};
		for (std::size_t i = 0; i < N; ++i) {
			auto& slot = slots_[i];
			if (slot.state != SlotState::Free)
				continue;
			if (slot.generation == UINT32_MAX || slot.serial == UINT32_MAX) {
				slot.state = SlotState::Retired;
				continue;
			}
			++slot.generation;
			slot.connection = connection;
			slot.state = SlotState::Active;
			slot.pendingTicket = 0;
			slot.acknowledged = false;
			return {RpcSessionStatus::Ok, session(i)};
		}
		return {RpcSessionStatus::NoCapacity};
	}

	[[nodiscard]] constexpr bool valid(RpcSessionHandle handle) const noexcept
	{
		return handle.owner == this && handle.index < N &&
		       slots_[handle.index].state == SlotState::Active &&
		       slots_[handle.index].generation == handle.generation &&
		       slots_[handle.index].connection == handle.connection;
	}

	[[nodiscard]] RpcSessionStatus release(RpcSessionHandle handle) noexcept
	{
		if (!valid(handle))
			return RpcSessionStatus::InvalidSession;
		auto& slot = slots_[handle.index];
		slot.state = SlotState::Closing;
		slot.pendingTicket = 0;
		slot.acknowledged = false;
		if (bindings_[handle.index].mailbox->state() == RpcState::Running)
			return RpcSessionStatus::Closing;
		finishClosing(handle.index);
		return RpcSessionStatus::Ok;
	}

	// Request and reply buffers must satisfy the existing Resource/provider
	// aliasing contracts. Neither span is retained. The application supplies a
	// fresh process(nowMs) tick before ingress when no callback was dispatched.
	[[nodiscard]] RpcSessionHandleResult handle(RpcSessionHandle handle, Input request,
	                                            Output response) noexcept
	{
		if (processing_ || handling_)
			return {RpcSessionStatus::Reentrant};
		if (!valid(handle))
			return {RpcSessionStatus::InvalidSession};
		auto& slot = slots_[handle.index];
		const auto& binding = bindings_[handle.index];
		// Snapshot envelope fields before Resource overwrites an overlapping reply.
		const bool finalWrite =
		    request.size() >= protocol::wire::writeRequestHeaderSize &&
		    request[0] == std::byte(protocol::Op::Write) && request[13] == std::byte{1} &&
		    protocol::client::detail::load<FileIndex>(request, 1) == binding.rpcFile;
		const std::uint32_t submitted =
		    finalWrite ? protocol::client::detail::load<std::uint16_t>(request, 14) : 0;
		if (finalWrite && slot.serial == UINT32_MAX)
			return {RpcSessionStatus::Exhausted};
		const auto before = binding.mailbox->state();
		handling_ = true;
		const auto reply = protocol::process(binding.files, request, response);
		handling_ = false;
		if (!valid(handle))
			return {RpcSessionStatus::InvalidSession, reply};
		RpcReplyTicket ticket{};
		if (before != RpcState::Pending && binding.mailbox->state() == RpcState::Pending) {
			slot.pendingTicket = 0;
			slot.acknowledged = false;
		}
		// Only an exact successful final WRITE reply may open the gate. In
		// particular a later READ, failed WRITE or another file's WRITE cannot.
		if (binding.mailbox->state() == RpcState::Pending && finalWrite &&
		    reply.status == Status::Ok && reply.written == protocol::wire::writeReplySize) {
			const auto ack =
			    protocol::client::parseWrite(Input{response}.first(reply.written), submitted);
			if (ack && !ack.shortError && ack.response.status == Status::Ok &&
			    ack.response.complete && ack.response.consumed == submitted) {
				ticket = {this, handle.index, handle.generation, ++slot.serial};
				slot.pendingTicket = ticket.serial;
			}
		}
		return {RpcSessionStatus::Ok, reply, ticket};
	}

	// Call only after transport-owned storage has accepted this exact reply.
	// A newer final-WRITE retry replaces the preceding ticket. READs and refused
	// writes preserve it. Failure to hand off a reply requires no notification.
	[[nodiscard]] RpcSessionStatus replyAccepted(RpcSessionHandle handle,
	                                             RpcReplyTicket ticket) noexcept
	{
		if (!valid(handle))
			return RpcSessionStatus::InvalidSession;
		auto& slot = slots_[handle.index];
		if (ticket.owner != this || ticket.index != handle.index ||
		    ticket.generation != handle.generation || ticket.serial == 0 ||
		    ticket.serial != slot.pendingTicket ||
		    bindings_[handle.index].mailbox->state() != RpcState::Pending)
			return RpcSessionStatus::StaleReply;
		slot.acknowledged = true;
		slot.pendingTicket = 0;
		return RpcSessionStatus::Ok;
	}

	// One caller owns all pool operations. Reentrant process() is refused so a
	// shared Workspace never nests dispatch. A synchronous callback is not
	// preempted; after dispatch obtain fresh application time before ingress.
	[[nodiscard]] RpcSessionProcessResult process(std::uint32_t nowMs,
	                                              std::size_t maxCallbacks = 1) noexcept
	{
		if (processing_ || handling_)
			return {RpcSessionStatus::Reentrant};
		processing_ = true;
		for (std::size_t i = 0; i < N; ++i) {
			if (slots_[i].state == SlotState::Closing)
				finishClosing(i);
			if (slots_[i].state == SlotState::Active &&
			    bindings_[i].mailbox->state() != RpcState::Pending)
				bindings_[i].mailbox->process(nowMs);
		}
		RpcSessionProcessResult result{RpcSessionStatus::Ok};
		while (result.visited < N && result.dispatched < maxCallbacks) {
			const auto i = next_;
			next_ = next_ + 1 == N ? 0 : next_ + 1;
			++result.visited;
			auto& slot = slots_[i];
			if (slot.state != SlotState::Active || !slot.acknowledged ||
			    bindings_[i].mailbox->state() != RpcState::Pending)
				continue;
			slot.acknowledged = false;
			slot.pendingTicket = 0;
			bindings_[i].mailbox->process(nowMs);
			++result.dispatched;
			if (slot.state == SlotState::Closing)
				finishClosing(i);
		}
		processing_ = false;
		return result;
	}

private:
	enum class SlotState : std::uint8_t {
		Free,
		Active,
		Closing,
		Retired
	};

	struct Slot {
		RpcConnectionKey connection{};
		std::uint32_t generation = 0;
		std::uint32_t serial = 0;
		std::uint32_t pendingTicket = 0;
		SlotState state = SlotState::Free;
		bool acknowledged = false;
	};

	[[nodiscard]] constexpr RpcSessionHandle session(std::size_t index) const noexcept
	{
		return {index, slots_[index].generation, slots_[index].connection, this};
	}

	void finishClosing(std::size_t index) noexcept
	{
		if (bindings_[index].mailbox->state() == RpcState::Running)
			return;
		bindings_[index].mailbox->resetSession();
		slots_[index].state =
		    slots_[index].generation == UINT32_MAX || slots_[index].serial == UINT32_MAX
		        ? SlotState::Retired
		        : SlotState::Free;
	}

	const std::array<RpcSessionBinding, N> bindings_;
	std::array<Slot, N> slots_;
	std::size_t next_ = 0;
	bool processing_ = false;
	bool handling_ = false;
};

} // namespace resource::telemetry::v3
#endif // RESOURCE_TELEMETRY_V3_RPC_SESSION_POOL_HPP
