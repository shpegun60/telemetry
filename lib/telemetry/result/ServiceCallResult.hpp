/*
 * @file ServiceCallResult.hpp
 * @brief Flat native Service outcomes over directly constructed nested storage.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_RESULT_SERVICE_CALL_RESULT_HPP
#define TELEMETRY_RESULT_SERVICE_CALL_RESULT_HPP
#pragma once

#include "NativeCallResult.hpp"
#include "ServiceResult.hpp"
#include "BorrowedServiceResult.hpp"
#include "../type/Traits.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <type_traits>
#include <utility>

namespace telemetry {

// Native routing and application outcomes share one public status here. These
// codes are separate from every encoded, application and selection status.
enum class ServiceCallStatus : std::uint8_t {
	Ok,
	InvalidArgument,
	Unavailable,
	Busy,
	Failed,
	NotFound,
	SignatureMismatch
};

namespace result_detail {

// Map application outcomes explicitly, without depending on enum numbering.
inline ServiceCallStatus serviceCallStatus(ServiceStatus status) noexcept
{
	switch (status) {
		case ServiceStatus::Ok:
			return ServiceCallStatus::Ok;
		case ServiceStatus::InvalidArgument:
			return ServiceCallStatus::InvalidArgument;
		case ServiceStatus::Unavailable:
			return ServiceCallStatus::Unavailable;
		case ServiceStatus::Busy:
			return ServiceCallStatus::Busy;
		case ServiceStatus::Failed:
			return ServiceCallStatus::Failed;
	}
	std::abort();
}

template<class Result>
ServiceCallStatus serviceCallStatus(const NativeCallResult<Result>& result) noexcept
{
	switch (result.status()) {
		case NativeCallStatus::Ok:
			return serviceCallStatus(result.value().status());
		case NativeCallStatus::NotFound:
			return ServiceCallStatus::NotFound;
		case NativeCallStatus::SignatureMismatch:
			return ServiceCallStatus::SignatureMismatch;
	}
	std::abort();
}

} // namespace result_detail

// Owns the original nested result in final member storage. fromNative accepts
// an exact prvalue factory, so flattening never materializes or moves Response.
// hasValue() reports both successful routing and application success. value()
// requires hasValue(); the void form checks that precondition without a payload.
// Public methods:
// - fromNative(): Construct nested result directly in final storage.
// - status(): Read the flat outcome.
// - hasValue(): Check operation success and payload lifetime.
// - operator bool(): Check operation success.
// - value(): Access the successful response, or check a void success.
// - valueOrNull(): Access an optional nonvoid response pointer.
template<class T>
class ServiceCallResult {
	static_assert(std::is_same_v<T, std::remove_cvref_t<T>> &&
	                  (std::is_void_v<T> || std::is_object_v<T>),
	              "ServiceCallResult requires an unqualified payload type or void");
	using NativeResult = NativeCallResult<ServiceResult<T>>;

	struct FactoryTag {};

public:
	using value_type = T;

	template<class Factory>
	[[nodiscard]] static ServiceCallResult
	fromNative(Factory&& factory) noexcept(std::is_nothrow_invocable_v<Factory&&>)
	{
		static_assert(std::is_same_v<std::invoke_result_t<Factory&&>, NativeResult>,
		              "ServiceCallResult factory must return the exact native result type");
		return ServiceCallResult{FactoryTag{}, std::forward<Factory>(factory)};
	}

	[[nodiscard]] ServiceCallStatus status() const noexcept
	{
		return result_detail::serviceCallStatus(native_);
	}

	[[nodiscard]] bool hasValue() const noexcept
	{
		return native_.hasValue() && native_.value().hasValue();
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return hasValue();
	}

	[[nodiscard]] std::add_lvalue_reference_t<T> value() & noexcept
	    requires(!std::is_void_v<T>)
	{
		requireValue();
		return native_.value().value();
	}

	[[nodiscard]] std::add_lvalue_reference_t<const T> value() const& noexcept
	    requires(!std::is_void_v<T>)
	{
		requireValue();
		return native_.value().value();
	}

	[[nodiscard]] std::add_rvalue_reference_t<T> value() && noexcept
	    requires(!std::is_void_v<T>)
	{
		requireValue();
		return std::move(native_.value().value());
	}

	[[nodiscard]] std::add_rvalue_reference_t<const T> value() const&& noexcept
	    requires(!std::is_void_v<T>)
	{
		requireValue();
		return std::move(native_.value().value());
	}

	void value() const& noexcept
	    requires std::is_void_v<T>
	{
		requireValue();
	}

	[[nodiscard]] std::add_pointer_t<T> valueOrNull() noexcept
	    requires(!std::is_void_v<T>)
	{
		return native_.hasValue() ? native_.value().valueOrNull() : nullptr;
	}

	[[nodiscard]] std::add_pointer_t<const T> valueOrNull() const noexcept
	    requires(!std::is_void_v<T>)
	{
		return native_.hasValue() ? native_.value().valueOrNull() : nullptr;
	}

private:
	template<class Factory>
	ServiceCallResult(FactoryTag,
	                  Factory&& factory) noexcept(std::is_nothrow_invocable_v<Factory&&>)
	    : native_(std::invoke(std::forward<Factory>(factory)))
	{}

	void requireValue() const noexcept
	{
		if (!hasValue())
			std::abort();
	}

	NativeResult native_;
};

// Owns only the original routing/result wrappers, never the borrowed response.
// The application must keep Response alive and synchronized for every view use,
// exactly as for BorrowedServiceResult. No conversion to an owning result exists.
// Public methods:
// - fromNative(): Construct the exact borrowed native result.
// - status(): Read the flat outcome.
// - hasValue(): Check routing and borrowed response presence.
// - operator bool(): Check response presence.
// - value(): Borrow the successful const response.
// - valueOrNull(): Borrow an optional const response pointer.
template<class T>
class BorrowedServiceCallResult {
	static_assert(std::is_same_v<T, std::remove_cvref_t<T>> && std::is_object_v<T>,
	              "BorrowedServiceCallResult requires an unqualified nonvoid payload type");
	using NativeResult = NativeCallResult<BorrowedServiceResult<T>>;

	struct FactoryTag {};

public:
	using value_type = T;

	template<class Factory>
	[[nodiscard]] static BorrowedServiceCallResult
	fromNative(Factory&& factory) noexcept(std::is_nothrow_invocable_v<Factory&&>)
	{
		static_assert(std::is_same_v<std::invoke_result_t<Factory&&>, NativeResult>,
		              "BorrowedServiceCallResult factory must return the exact native result type");
		return BorrowedServiceCallResult{FactoryTag{}, std::forward<Factory>(factory)};
	}

	[[nodiscard]] ServiceCallStatus status() const noexcept
	{
		return result_detail::serviceCallStatus(native_);
	}

	[[nodiscard]] bool hasValue() const noexcept
	{
		return native_.hasValue() && native_.value().hasValue();
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return hasValue();
	}

	[[nodiscard]] const T& value() const noexcept
	{
		if (!hasValue())
			std::abort();
		return native_.value().value();
	}

	[[nodiscard]] const T* valueOrNull() const noexcept
	{
		return native_.hasValue() ? native_.value().valueOrNull() : nullptr;
	}

private:
	template<class Factory>
	BorrowedServiceCallResult(FactoryTag,
	                          Factory&& factory) noexcept(std::is_nothrow_invocable_v<Factory&&>)
	    : native_(std::invoke(std::forward<Factory>(factory)))
	{}

	NativeResult native_;
};

namespace detail {

// Existing result wrappers are classes but not aggregate response payloads.
// Short-circuit before Type<T> to leave their low-level overloads unchanged.
template<class T>
concept flatServiceResponse = std::is_same_v<T, std::remove_cvref_t<T>> &&
                              (std::is_void_v<T> || (std::is_class_v<T> && std::is_aggregate_v<T> &&
                                                     classify<T>() == TypeClass::Struct &&
                                                     Type<T>::kind == TypeKind::Struct));

} // namespace detail
} // namespace telemetry

#endif
