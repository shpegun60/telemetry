/*
 * @file FieldReadResult.hpp
 * @brief Native Field read status and an explicitly owned value on success.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Reports lookup, type, availability and conversion failures separately.
 *
 * An inactive union avoids constructing T for a failed read. successFrom
 * constructs the requested owning value directly in final storage, including
 * large native structures. These statuses describe native convenience access;
 * they are independent of endpoint and encoded dispatch status codes.
 */

#ifndef TELEMETRY_RESULT_FIELD_READ_RESULT_HPP
#define TELEMETRY_RESULT_FIELD_READ_RESULT_HPP
#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace telemetry {

enum class FieldReadStatus : std::uint8_t {
	Ok,
	NotFound,
	TypeMismatch,
	Unavailable,
	ConversionFailed
};

// Engaged exactly when status() is Ok. Failed reads never construct T;
// successful reads explicitly own T and do not extend any lifetime T borrows.
// Dereferencing requires hasValue(); valueOrNull() is safe on either state.
// Public methods:
// - successFrom(): Construct requested owning value.
// - failure(): Store native read failure.
// - FieldReadResult(): Preserve result state.
// - operator=(): Replace result state.
// - ~FieldReadResult(): Destroy active value.
// - status(): Read native status.
// - hasValue(): Check owned value lifetime.
// - operator bool(): Check owned value lifetime.
// - value(): Access active value.
// - valueOrNull(): Access optional pointer.
// - operator*(): Access active value.
// - operator->(): Access present pointer.
template<class T>
class FieldReadResult {
	static_assert(std::is_object_v<T> && std::is_same_v<T, std::remove_cvref_t<T>>,
	              "FieldReadResult requires an unqualified object type");
	static_assert(std::is_nothrow_destructible_v<T>,
	              "FieldReadResult value destructor must be noexcept");

public:
	using value_type = T;

	template<class Factory>
	[[nodiscard]] static FieldReadResult successFrom(Factory&& factory)
	{
		static_assert(std::is_same_v<std::invoke_result_t<Factory&&>, T>,
		              "FieldReadResult factory must return the exact value type");
		return FieldReadResult{SuccessTag{}, std::forward<Factory>(factory)};
	}

	[[nodiscard]] static FieldReadResult failure(FieldReadStatus status) noexcept
	{
		if (status == FieldReadStatus::Ok ||
		    static_cast<std::uint8_t>(status) >
		        static_cast<std::uint8_t>(FieldReadStatus::ConversionFailed))
			std::abort();
		return FieldReadResult{status};
	}

	FieldReadResult(const FieldReadResult& other)
	    requires std::is_copy_constructible_v<T>
	    : status_(other.status_)
	{
		if (other.hasValue())
			::new (static_cast<void*>(std::addressof(payload_.value))) T(other.payload_.value);
	}

	FieldReadResult(FieldReadResult&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
	    requires std::is_move_constructible_v<T>
	    : status_(other.status_)
	{
		if (other.hasValue())
			::new (static_cast<void*>(std::addressof(payload_.value)))
			    T(std::move(other.payload_.value));
	}

	FieldReadResult& operator=(const FieldReadResult& other)
	    requires(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>)
	{
		if (this == std::addressof(other))
			return *this;
		if (hasValue() && other.hasValue()) {
			payload_.value = other.payload_.value;
		} else if (hasValue()) {
			std::destroy_at(std::addressof(payload_.value));
		} else if (other.hasValue()) {
			::new (static_cast<void*>(std::addressof(payload_.value))) T(other.payload_.value);
		}
		status_ = other.status_;
		return *this;
	}

	FieldReadResult&
	operator=(FieldReadResult&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
	                                            std::is_nothrow_move_assignable_v<T>)
	    requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
	{
		if (this == std::addressof(other))
			return *this;
		if (hasValue() && other.hasValue()) {
			payload_.value = std::move(other.payload_.value);
		} else if (hasValue()) {
			std::destroy_at(std::addressof(payload_.value));
		} else if (other.hasValue()) {
			::new (static_cast<void*>(std::addressof(payload_.value)))
			    T(std::move(other.payload_.value));
		}
		status_ = other.status_;
		return *this;
	}

	~FieldReadResult()
	{
		if (hasValue())
			std::destroy_at(std::addressof(payload_.value));
	}

	[[nodiscard]] FieldReadStatus status() const noexcept
	{
		return status_;
	}

	[[nodiscard]] bool hasValue() const noexcept
	{
		return status_ == FieldReadStatus::Ok;
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return hasValue();
	}

	[[nodiscard]] T& value() & noexcept
	{
		return payload_.value;
	}

	[[nodiscard]] const T& value() const& noexcept
	{
		return payload_.value;
	}

	[[nodiscard]] T&& value() && noexcept
	{
		return std::move(payload_.value);
	}

	[[nodiscard]] T* valueOrNull() noexcept
	{
		return hasValue() ? std::addressof(payload_.value) : nullptr;
	}

	[[nodiscard]] const T* valueOrNull() const noexcept
	{
		return hasValue() ? std::addressof(payload_.value) : nullptr;
	}

	[[nodiscard]] T& operator*() & noexcept
	{
		return value();
	}

	[[nodiscard]] const T& operator*() const& noexcept
	{
		return value();
	}

	[[nodiscard]] T&& operator*() && noexcept
	{
		return std::move(*this).value();
	}

	[[nodiscard]] T* operator->() noexcept
	{
		return valueOrNull();
	}

	[[nodiscard]] const T* operator->() const noexcept
	{
		return valueOrNull();
	}

private:
	// Selects direct value construction in final storage.
	struct SuccessTag {};

	// The inert member starts union lifetime without initializing T.
	// Public methods:
	// - Payload(): Start inert member.
	// - ~Payload(): Defer value destruction.
	union Payload {
		char empty;
		T value;

		constexpr Payload() noexcept : empty{}
		{}

		~Payload()
		{}
	};

	explicit FieldReadResult(FieldReadStatus status) noexcept : status_(status)
	{}

	template<class Factory>
	FieldReadResult(SuccessTag, Factory&& factory) : status_(FieldReadStatus::Ok)
	{
		::new (static_cast<void*>(std::addressof(payload_.value)))
		    T(std::forward<Factory>(factory)());
	}

	FieldReadStatus status_;
	Payload payload_;
};

} // namespace telemetry

#endif
