/* Bounded stable request records and a shared byte pool. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#ifndef TELEMETRY_LIB_TELEMETRY_CLIENT_REQUESTQUEUE_HPP
#define TELEMETRY_LIB_TELEMETRY_CLIENT_REQUESTQUEUE_HPP
#pragma once
#include "Backend.hpp"
#include <common/DelegateSlot.hpp>
#include <array>
#include <limits>

namespace telemetry::client {
struct NoLock {
	void lock() noexcept
	{}

	void unlock() noexcept
	{}
};

namespace detail {
template<class Lock>
class Guard {
	Lock& lock_;

public:
	explicit Guard(Lock& lock) noexcept : lock_(lock)
	{
		lock_.lock();
	}

	~Guard()
	{
		lock_.unlock();
	}
};
} // namespace detail

// Only reservation/publication/accounting use Lock. Snapshot encoding, closure
// construction/destruction, backend work and callbacks run without it. Slots
// never move. FIFO order is the order of successful publication, not reservation.
template<std::size_t N, std::size_t PayloadBytes, std::size_t CallbackBytes = 64,
         class Lock = NoLock>
class RequestQueue {
	static_assert(N > 0);

public:
	enum class State : std::uint8_t {
		Free,
		Reserved,
		Queued,
		Active,
		Completing
	};

	struct Record {
		State state = State::Free;
		std::size_t offset = 0, bytes = 0;
		std::uint32_t generation = 0, deadline = 0;
		std::uint64_t sequence = 0;
		Operation operation = Operation::FieldRead;
		std::uint32_t endpoint = 0, responseBytes = 0;
		bool cancelled = false;
		DelegateSlot<void(const ResultView&, Output) noexcept, CallbackBytes> callback;
	};

	struct Reservation {
		Status status;
		Record* record;
	};

	RequestQueue() = default;
	RequestQueue(const RequestQueue&) = delete;
	RequestQueue& operator=(const RequestQueue&) = delete;

	[[nodiscard]] Reservation reserve(std::size_t bytes) noexcept
	{
		detail::Guard guard(lock_);
		if (closing_)
			return {Status::Closed, nullptr};
		if (!admitted_)
			return {admissionStatus_, nullptr};
		if (count_ == N)
			return {Status::QueueFull, nullptr};
		if (bytes > PayloadBytes)
			return {Status::PayloadFull, nullptr};
		std::size_t offset = 0;
		// At most N occupied intervals. Jump beyond overlaps, without allocating
		// per-byte metadata or giving every request a maximum-sized buffer.
		for (std::size_t pass = 0; pass <= N; ++pass) {
			bool collision = false;
			for (const auto& record : records_) {
				if (record.state != State::Free && record.bytes && bytes &&
				    offset < record.offset + record.bytes && record.offset < offset + bytes) {
					offset = record.offset + record.bytes;
					collision = true;
				}
			}
			if (offset > PayloadBytes - bytes)
				return {Status::PayloadFull, nullptr};
			if (!collision)
				break;
		}
		for (auto& record : records_)
			if (record.state == State::Free) {
				record.state = State::Reserved;
				record.offset = offset;
				record.bytes = bytes;
				record.generation = generation_;
				record.cancelled = false;
				++count_;
				++reservations_;
				return {Status::Ok, &record};
			}
		return {Status::QueueFull, nullptr};
	}

	[[nodiscard]] Output payload(Record& record) noexcept
	{
		return Output{pool_}.subspan(record.offset, record.bytes);
	}

	[[nodiscard]] Submission publish(Record& record) noexcept
	{
		detail::Guard guard(lock_);
		if (closing_ || !admitted_ || record.generation != generation_)
			return {closing_ ? Status::Closed : Status::NotAdmitted, {}};
		if (sequence_ == std::numeric_limits<std::uint64_t>::max())
			return {Status::IdsExhausted, {}};
		record.sequence = ++sequence_;
		record.state = State::Queued;
		--reservations_;
		return {Status::Ok, {this, record.sequence, record.generation}};
	}

	// The caller has exclusive ownership of a Reserved/Completing record.
	void release(Record& record) noexcept
	{
		record.callback.reset();
		detail::Guard guard(lock_);
		if (record.state == State::Reserved)
			--reservations_;
		record.state = State::Free;
		record.bytes = 0;
		--count_;
	}

	[[nodiscard]] Record* takeNext() noexcept
	{
		detail::Guard guard(lock_);
		Record* next = nullptr;
		for (auto& record : records_)
			if (record.state == State::Queued && (!next || record.sequence < next->sequence))
				next = &record;
		if (next)
			next->state = State::Active;
		return next;
	}

	[[nodiscard]] Record* takeExpired(std::uint32_t now, bool cancelAll) noexcept
	{
		detail::Guard guard(lock_);
		Record* next = nullptr;
		for (auto& record : records_)
			if (record.state == State::Queued &&
			    (cancelAll || record.cancelled || expired(now, record.deadline)) &&
			    (!next || record.sequence < next->sequence))
				next = &record;
		if (next)
			next->state = State::Completing;
		return next;
	}

	[[nodiscard]] bool cancel(RequestHandle handle) noexcept
	{
		detail::Guard guard(lock_);
		if (handle.owner != this || !handle.sequence)
			return false;
		for (auto& record : records_)
			if ((record.state == State::Queued || record.state == State::Active) &&
			    record.sequence == handle.sequence && record.generation == handle.generation) {
				record.cancelled = true;
				return true;
			}
		return false;
	}

	[[nodiscard]] bool cancelled(const Record& record) const noexcept
	{
		detail::Guard guard(lock_);
		return record.cancelled;
	}

	[[nodiscard]] RequestHandle handle(const Record& record) const noexcept
	{
		return {this, record.sequence, record.generation};
	}

	void completing(Record& record) noexcept
	{
		detail::Guard guard(lock_);
		record.state = State::Completing;
	}

	[[nodiscard]] bool admit(std::uint32_t generation) noexcept
	{
		detail::Guard guard(lock_);
		if (closing_ || count_ || !generation)
			return false;
		generation_ = generation;
		admitted_ = true;
		admissionStatus_ = Status::Ok;
		return true;
	}

	[[nodiscard]] Status beginAdmission() noexcept
	{
		detail::Guard guard(lock_);
		if (closing_)
			return Status::Closed;
		if (count_)
			return Status::Busy;
		admitted_ = false;
		admissionStatus_ = Status::NotAdmitted;
		return Status::Ok;
	}

	void invalidate(Status reason = Status::NotAdmitted) noexcept
	{
		detail::Guard guard(lock_);
		admitted_ = false;
		admissionStatus_ = reason;
	}

	void close() noexcept
	{
		detail::Guard guard(lock_);
		closing_ = true;
		admitted_ = false;
	}

	[[nodiscard]] bool closing() const noexcept
	{
		detail::Guard guard(lock_);
		return closing_;
	}

	[[nodiscard]] bool admitted() const noexcept
	{
		detail::Guard guard(lock_);
		return admitted_;
	}

	void markDrained() noexcept
	{
		detail::Guard guard(lock_);
		if (closing_ && count_ == 0)
			drained_ = true;
	}

	[[nodiscard]] bool drained() const noexcept
	{
		detail::Guard guard(lock_);
		return drained_;
	}

	[[nodiscard]] std::size_t size() const noexcept
	{
		detail::Guard guard(lock_);
		return count_;
	}

	[[nodiscard]] std::size_t reservations() const noexcept
	{
		detail::Guard guard(lock_);
		return reservations_;
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return size() == 0;
	}

private:
	mutable Lock lock_{};
	// Record initializes every member; redundant value-init triggers a MinGW 13.1 ICE.
	std::array<Record, N> records_;
	std::array<std::byte, PayloadBytes> pool_{};
	std::size_t count_ = 0, reservations_ = 0;
	std::uint64_t sequence_ = 0;
	std::uint32_t generation_ = 0;
	bool admitted_ = false, closing_ = false, drained_ = false;
	Status admissionStatus_ = Status::NotAdmitted;
};
} // namespace telemetry::client
#endif // TELEMETRY_LIB_TELEMETRY_CLIENT_REQUESTQUEUE_HPP
