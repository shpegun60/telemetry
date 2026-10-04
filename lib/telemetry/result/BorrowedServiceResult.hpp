/*
 * @file BorrowedServiceResult.hpp
 * @brief Service status and a const view, without response materialization.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Service application status paired with a borrowed const response view.
 *
 * Success borrows an exact response lvalue; failure carries no response and
 * must use a non-Ok status. This result avoids constructing an owning response
 * wrapper, but the application must keep the response alive and stable for
 * every native use or encoded serialization of the returned view.
 */

#ifndef TELEMETRY_RESULT_BORROWED_SERVICE_RESULT_HPP
#define TELEMETRY_RESULT_BORROWED_SERVICE_RESULT_HPP
#pragma once

#include "BorrowedValue.hpp"
#include "ServiceResult.hpp"

namespace telemetry {

// Unlike ServiceResult<T>, this result neither owns nor destroys T. Native
// callers must keep the response alive after call() for all their view uses.
// Public methods:
// - success(): Borrow response lvalue.
// - failure(): Store failure status.
// - status(): Read application status.
// - hasValue(): Check response presence.
// - operator bool(): Check response presence.
// - valueOrNull(): Borrow optional pointer.
// - value(): Borrow present response.
// - operator*(): Borrow present response.
// - operator->(): Borrow present pointer.
template<class T>
class BorrowedServiceResult {
public:
	using value_type = T;

	template<class... Explicit, class Value>
	    requires(sizeof...(Explicit) == 0 &&
	             requires(Value&& value) { BorrowedValue<T>::from(std::forward<Value>(value)); })
	[[nodiscard]] static constexpr BorrowedServiceResult success(Value&& value) noexcept
	{
		return BorrowedServiceResult{ServiceStatus::Ok,
		                             BorrowedValue<T>::from(std::forward<Value>(value))};
	}

	[[nodiscard]] static BorrowedServiceResult failure(ServiceStatus status) noexcept
	{
		result_detail::requireFailure(status);
		return BorrowedServiceResult{status, {}};
	}

	// Dereferencing requires hasValue(); valueOrNull() is safe on failure.
	// The view never makes the response immutable against other owner access.
	[[nodiscard]] constexpr ServiceStatus status() const noexcept
	{
		return status_;
	}

	[[nodiscard]] constexpr bool hasValue() const noexcept
	{
		return value_.hasValue();
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return hasValue();
	}

	[[nodiscard]] constexpr const T* valueOrNull() const noexcept
	{
		return value_.valueOrNull();
	}

	[[nodiscard]] constexpr const T& value() const noexcept
	{
		return value_.value();
	}

	[[nodiscard]] constexpr const T& operator*() const noexcept
	{
		return value();
	}

	[[nodiscard]] constexpr const T* operator->() const noexcept
	{
		return valueOrNull();
	}

private:
	constexpr BorrowedServiceResult(ServiceStatus status, BorrowedValue<T> value) noexcept
	    : status_(status), value_(value)
	{}

	ServiceStatus status_;
	BorrowedValue<T> value_;
};

} // namespace telemetry

#endif
