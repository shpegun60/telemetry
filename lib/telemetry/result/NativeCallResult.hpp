/*
 * @file NativeCallResult.hpp
 * @brief Native routing status and a directly constructed endpoint result.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_RESULT_NATIVE_CALL_RESULT_HPP
#define TELEMETRY_RESULT_NATIVE_CALL_RESULT_HPP
#pragma once

#include <cstdlib>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace telemetry {

// This status describes native selection, not application success. An Ok
// selection may contain Busy, Unavailable or any other endpoint outcome.
enum class NativeCallStatus : std::uint8_t {
	Ok,
	NotFound,
	SignatureMismatch
};

// Owns the exact endpoint Result only after successful native selection.
// successFrom constructs that Result in final union storage, without an
// intermediate optional, default object or extra move of a large response.
// Borrowed result wrappers retain their original application lifetimes.
// Public methods:
// - successFrom(): Construct selected result.
// - failure(): Report selection failure.
// - NativeCallResult(): Preserve result state.
// - operator=(): Replace result state.
// - ~NativeCallResult(): Destroy active result.
// - status(): Read selection status.
// - hasValue(): Check endpoint delivery.
// - value(): Access endpoint result.
// - valueOrNull(): Access optional result.
template<class T>
class NativeCallResult {
	static_assert(std::is_same_v<T, std::remove_cvref_t<T>> && !std::is_void_v<T>,
	              "NativeCallResult requires a non-cv object result");
	static_assert(std::is_nothrow_destructible_v<T>,
	              "NativeCallResult result destructor must be noexcept");

	struct SuccessTag {};

	union Storage {
		char empty;
		T result;

		constexpr Storage() noexcept : empty{}
		{}

		template<class Factory>
		explicit Storage(SuccessTag,
		                 Factory&& factory) noexcept(std::is_nothrow_invocable_v<Factory&&>)
		    : result(std::invoke(std::forward<Factory>(factory)))
		{}

		~Storage()
		    requires std::is_trivially_destructible_v<T>
		= default;

		~Storage()
		    requires(!std::is_trivially_destructible_v<T>)
		{}
	};

public:
	using value_type = T;

	template<class Factory>
	[[nodiscard]] static NativeCallResult
	successFrom(Factory&& factory) noexcept(std::is_nothrow_invocable_v<Factory&&>)
	{
		static_assert(std::is_same_v<std::invoke_result_t<Factory&&>, T>,
		              "NativeCallResult factory must return the exact result type");
		return NativeCallResult{SuccessTag{}, std::forward<Factory>(factory)};
	}

	[[nodiscard]] static NativeCallResult failure(NativeCallStatus status) noexcept
	{
		if (status != NativeCallStatus::NotFound && status != NativeCallStatus::SignatureMismatch)
			std::abort();
		return NativeCallResult{status};
	}

	NativeCallResult(const NativeCallResult&)
	    requires std::is_trivially_copy_constructible_v<T>
	= default;

	NativeCallResult(const NativeCallResult& other) noexcept(
	    std::is_nothrow_copy_constructible_v<T>)
	    requires(std::is_copy_constructible_v<T> && !std::is_trivially_copy_constructible_v<T>)
	    : status_(other.status_)
	{
		if (hasValue())
			::new (static_cast<void*>(std::addressof(storage_.result))) T(other.storage_.result);
	}

	NativeCallResult(NativeCallResult&&)
	    requires std::is_trivially_move_constructible_v<T>
	= default;

	NativeCallResult(NativeCallResult&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
	    requires(std::is_move_constructible_v<T> && !std::is_trivially_move_constructible_v<T>)
	    : status_(other.status_)
	{
		if (hasValue())
			::new (static_cast<void*>(std::addressof(storage_.result)))
			    T(std::move(other.storage_.result));
	}

	NativeCallResult& operator=(const NativeCallResult&)
	    requires(std::is_trivially_copy_assignable_v<T> &&
	             std::is_trivially_copy_constructible_v<T>)
	= default;

	NativeCallResult&
	operator=(const NativeCallResult& other) noexcept(std::is_nothrow_copy_constructible_v<T> &&
	                                                  std::is_nothrow_copy_assignable_v<T>)
	    requires(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T> &&
	             !(std::is_trivially_copy_assignable_v<T> &&
	               std::is_trivially_copy_constructible_v<T>))
	{
		if (this != std::addressof(other)) {
			assign(other);
			status_ = other.status_;
		}
		return *this;
	}

	NativeCallResult& operator=(NativeCallResult&&)
	    requires(std::is_trivially_move_assignable_v<T> &&
	             std::is_trivially_move_constructible_v<T>)
	= default;

	NativeCallResult&
	operator=(NativeCallResult&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
	                                             std::is_nothrow_move_assignable_v<T>)
	    requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T> &&
	             !(std::is_trivially_move_assignable_v<T> &&
	               std::is_trivially_move_constructible_v<T>))
	{
		if (this != std::addressof(other)) {
			assign(std::move(other));
			status_ = other.status_;
		}
		return *this;
	}

	~NativeCallResult()
	    requires std::is_trivially_destructible_v<T>
	= default;

	~NativeCallResult()
	    requires(!std::is_trivially_destructible_v<T>)
	{
		if (hasValue())
			std::destroy_at(std::addressof(storage_.result));
	}

	[[nodiscard]] NativeCallStatus status() const noexcept
	{
		return status_;
	}

	[[nodiscard]] bool hasValue() const noexcept
	{
		return status_ == NativeCallStatus::Ok;
	}

	[[nodiscard]] T& value() & noexcept
	{
		requireValue();
		return storage_.result;
	}

	[[nodiscard]] const T& value() const& noexcept
	{
		requireValue();
		return storage_.result;
	}

	[[nodiscard]] T&& value() && noexcept
	{
		requireValue();
		return std::move(storage_.result);
	}

	[[nodiscard]] const T&& value() const&& noexcept
	{
		requireValue();
		return std::move(storage_.result);
	}

	[[nodiscard]] T* valueOrNull() noexcept
	{
		return hasValue() ? std::addressof(storage_.result) : nullptr;
	}

	[[nodiscard]] const T* valueOrNull() const noexcept
	{
		return hasValue() ? std::addressof(storage_.result) : nullptr;
	}

private:
	template<class Other>
	void assign(Other&& other)
	{
		if (hasValue() && other.hasValue())
			storage_.result = std::forward<Other>(other).storage_.result;
		else if (hasValue())
			std::destroy_at(std::addressof(storage_.result));
		else if (other.hasValue())
			::new (static_cast<void*>(std::addressof(storage_.result)))
			    T(std::forward<Other>(other).storage_.result);
	}

	void requireValue() const noexcept
	{
		if (!hasValue())
			std::abort();
	}

	explicit NativeCallResult(NativeCallStatus status) noexcept : status_(status)
	{}

	template<class Factory>
	NativeCallResult(SuccessTag tag,
	                 Factory&& factory) noexcept(std::is_nothrow_invocable_v<Factory&&>)
	    : status_(NativeCallStatus::Ok), storage_(tag, std::forward<Factory>(factory))
	{}

	NativeCallStatus status_;
	Storage storage_;
};

} // namespace telemetry

#endif
