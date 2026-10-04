/*
 * @file BorrowedValue.hpp
 * @brief Nullable const view of an existing native value, without ownership.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * An optional const view of an existing native object without materializing it.
 *
 * Factories accept exact object lvalues and reject temporaries or conversion
 * proxies. Copying the view copies a pointer and does not extend any lifetime
 * or freeze the object state. A Field with an unavailable borrowed getter uses
 * the empty view rather than a fabricated payload.
 */

#ifndef TELEMETRY_RESULT_BORROWED_VALUE_HPP
#define TELEMETRY_RESULT_BORROWED_VALUE_HPP
#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace telemetry {

// Copying the view copies only its pointer. The application keeps the object
// alive and stable for every use of the view; this is not a snapshot.
// Public methods:
// - BorrowedValue(): Start empty view.
// - from(): Borrow exact lvalue.
// - hasValue(): Check view presence.
// - operator bool(): Check view presence.
// - valueOrNull(): Borrow optional pointer.
// - value(): Borrow present value.
// - operator*(): Borrow present value.
// - operator->(): Borrow present pointer.
template<class T>
class BorrowedValue {
	static_assert(std::is_object_v<T> && !std::is_pointer_v<T> &&
	                  std::is_same_v<T, std::remove_cvref_t<T>>,
	              "BorrowedValue requires an unqualified object type");

public:
	using value_type = T;

	constexpr BorrowedValue() noexcept = default;

	// Deduce the actual argument, including its category. A const T& factory
	// alone would accept a temporary or a proxy conversion. The leading pack
	// also prevents explicit type arguments from hiding those categories.
	template<class... Explicit, class Value>
	    requires(sizeof...(Explicit) == 0 && std::is_lvalue_reference_v<Value> &&
	             std::is_same_v<std::remove_cvref_t<Value>, T> &&
	             !std::is_volatile_v<std::remove_reference_t<Value>>)
	[[nodiscard]] static constexpr BorrowedValue from(Value&& value) noexcept
	{
		return BorrowedValue{std::addressof(value)};
	}

	[[nodiscard]] constexpr bool hasValue() const noexcept
	{
		return value_ != nullptr;
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return hasValue();
	}

	[[nodiscard]] constexpr const T* valueOrNull() const noexcept
	{
		return value_;
	}

	// Precondition for dereferencing access: hasValue(). An unavailable
	// getter returns the empty view, never a fabricated zero-valued object.
	[[nodiscard]] constexpr const T& value() const noexcept
	{
		return *value_;
	}

	[[nodiscard]] constexpr const T& operator*() const noexcept
	{
		return value();
	}

	[[nodiscard]] constexpr const T* operator->() const noexcept
	{
		return value_;
	}

private:
	explicit constexpr BorrowedValue(const T* value) noexcept : value_(value)
	{}

	const T* value_ = nullptr;
};

} // namespace telemetry

#endif
