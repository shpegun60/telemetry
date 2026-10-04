/*
 * @file Workspace.hpp
 * @brief Caller-owned aligned storage and scoped lifetime for decoded values.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Caller-owned storage for native objects created during encoded operations.
 *
 * Aligned reservations and object construction are separate steps. A lease
 * destroys only the object it constructed and restores the previous cursor.
 * Reverse-order release makes nested operations possible without allocation;
 * the caller supplies capacity and serializes access to the Workspace.
 */

#ifndef TELEMETRY_CODEC_WORKSPACE_HPP
#define TELEMETRY_CODEC_WORKSPACE_HPP
#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <telemetry/core/Compiler.hpp>

namespace telemetry {

// The bound includes the worst possible initial alignment of a byte span.
// A caller may use less when it supplies an already aligned buffer.
template<class T>
inline constexpr std::size_t scratchBytes = [] {
	static_assert(sizeof(T) <= std::numeric_limits<std::size_t>::max() - (alignof(T) - 1),
	              "Workspace size calculation overflows size_t");
	return sizeof(T) + alignof(T) - 1;
}();

// A non-owning bump cursor over one caller-supplied byte span. The Workspace
// and its backing storage must outlive every lease. Access is externally
// serialized, and live leases must release in reverse reservation order.
// Public methods:
// - Workspace(): Borrow scratch storage.
// - storage(): Borrow scratch span.
// - used(): Report reserved bytes.
// - reserve(): Reserve aligned object.
class Workspace {
public:
	explicit constexpr Workspace(std::span<std::byte> storage) noexcept : storage_(storage)
	{}

	Workspace(const Workspace&) = delete;
	Workspace& operator=(const Workspace&) = delete;

	[[nodiscard]] constexpr std::span<std::byte> storage() const noexcept
	{
		return storage_;
	}

	[[nodiscard]] constexpr std::size_t used() const noexcept
	{
		return used_;
	}

	// Leases must be destroyed in reverse acquisition order. Ordinary local
	// variables provide that order. A live lease pins its reservation and,
	// once constructed, owns the C++ object's lifetime.
	// Public methods:
	// - Lease(): Reserve aligned storage.
	// - ~Lease(): Release leased object.
	// - valid(): Check reservation success.
	// - constructed(): Check object lifetime.
	// - get(): Borrow constructed object.
	// - workspaceStorage(): Borrow complete workspace.
	// - constructDefault(): Start trivial lifetime.
	// - constructFrom(): Construct factory result.
	template<class T>
	class Lease {
	public:
		static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>,
		              "Workspace lease requires a mutable object type");
		static_assert(std::is_nothrow_destructible_v<T>,
		              "Workspace lease requires a noexcept destructor");

		// Keep the reservation beside the fresh-lease state. At -Os an
		// outlined constructor hides that state and retains redundant object
		// checks and stack stores in every encoded operation.
		TELEMETRY_FORCE_INLINE explicit Lease(Workspace& workspace) noexcept
		    : workspace_(workspace), previous_(workspace.used_)
		{
			if (previous_ > workspace.storage_.size())
				return;
			if (workspace.storage_.size() - previous_ < sizeof(T))
				return;
			void* address = workspace.storage_.data() + previous_;
			std::size_t remaining = workspace.storage_.size() - previous_;
			void* aligned = std::align(alignof(T), sizeof(T), address, remaining);
			if (aligned == nullptr)
				return;

			storage_ = aligned;
			workspace.used_ = workspace.storage_.size() - remaining + sizeof(T);
		}

		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		Lease(Lease&&) = delete;
		Lease& operator=(Lease&&) = delete;

		~Lease()
		{
			if (object_ != nullptr)
				std::destroy_at(object_);
			if (storage_ != nullptr)
				workspace_.used_ = previous_;
		}

		// Reservation success does not imply construction: get() stays null
		// until one construction method starts T's lifetime. Invalid leases
		// consume no storage and leave the enclosing cursor unchanged.
		[[nodiscard]] bool valid() const noexcept
		{
			return storage_ != nullptr;
		}

		[[nodiscard]] bool constructed() const noexcept
		{
			return object_ != nullptr;
		}

		[[nodiscard]] T* get() const noexcept
		{
			return object_;
		}

		[[nodiscard]] std::span<std::byte> workspaceStorage() const noexcept
		{
			return workspace_.storage_;
		}

		// Default-initialization is only used for types whose construction
		// does no work. The codec must fill every member before any read.
		[[nodiscard]] T* constructDefault()
		{
			static_assert(std::is_trivially_default_constructible_v<T>,
			              "Default workspace construction requires a trivial constructor");
			if (storage_ == nullptr || object_ != nullptr)
				return nullptr;
			object_ = ::new (storage_) T;
			return object_;
		}

		// The factory's T prvalue initializes the final storage directly.
		// This avoids a named T temporary in the erased runtime helper.
		template<class Factory>
		[[nodiscard]] T* constructFrom(Factory&& factory)
		{
			static_assert(std::is_same_v<std::invoke_result_t<Factory&&>, T>,
			              "Workspace factory must return the exact object type");
			if (storage_ == nullptr || object_ != nullptr)
				return nullptr;
			object_ = ::new (storage_) T(std::forward<Factory>(factory)());
			return object_;
		}

	private:
		Workspace& workspace_;
		std::size_t previous_ = 0;
		void* storage_ = nullptr;
		T* object_ = nullptr;
	};

	// Reserve aligned bytes without creating T. A valid lease pins those
	// bytes until destruction, even if the caller never constructs an object.
	template<class T>
	[[nodiscard]] TELEMETRY_FORCE_INLINE Lease<T> reserve() noexcept
	{
		return Lease<T>{*this};
	}

private:
	std::span<std::byte> storage_;
	std::size_t used_ = 0;
};

} // namespace telemetry

#endif
