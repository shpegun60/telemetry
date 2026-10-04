/**
 * @file TelemetryFunctionSlot.h
 * @brief Stable slot for selecting a noexcept function at runtime.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */

/*
 * Late binding of one exact noexcept function pointer at a stable table address.
 *
 * get() snapshots the selected pointer by value and the endpoint adapter checks
 * it before invocation. A slot can be rebound without rebuilding descriptors
 * or changing declared capability. The slot owns no callback state and provides
 * no synchronization between binding and calls.
 */

#ifndef TELEMETRY_FUNCTION_SLOT_H
#define TELEMETRY_FUNCTION_SLOT_H
#pragma once

#include <type_traits>
#include <utility>
#include "TelemetrySlotTraits.h"
#include "../detail/Target.hpp"

namespace telemetry {

// Rejects signatures that are not R(Args...) noexcept; use the specialization.
template<class Signature>
class FunctionSlot {
	static_assert(!std::is_same_v<Signature, Signature>,
	              "FunctionSlot requires an R(Args...) noexcept function signature");
};

// Tables borrow the slot's stable address, not a snapshot of its function.
// Keep it alive for every table access. Bind/reset and access must be externally
// serialized; this pointer provides no synchronization or callback ownership.
// Adapters check get() before invocation; no unchecked operator() is exposed.
// Public methods:
// - FunctionSlot(): Start empty binding.
// - bind(): Select function pointer.
// - reset(): Clear selected function.
// - get(): Copy selected pointer.
// - available(): Check function presence.
// - operator bool(): Check function presence.
// - invoke(): Invoke engaged function.
template<class R, class... Args>
class FunctionSlot<R(Args...) noexcept> {
public:
	using Signature = R(Args...) noexcept;
	using Function = R (*)(Args...) noexcept;
	constexpr FunctionSlot() noexcept = default;
	FunctionSlot(const FunctionSlot&) = delete;
	FunctionSlot(FunctionSlot&&) = delete;
	FunctionSlot& operator=(const FunctionSlot&) = delete;
	FunctionSlot& operator=(FunctionSlot&&) = delete;

	constexpr void bind(Function function) noexcept
	{
		function_ = function;
	}

	constexpr void reset() noexcept
	{
		function_ = nullptr;
	}

	[[nodiscard]] constexpr Function get() const noexcept
	{
		return function_;
	}

	[[nodiscard]] constexpr bool available() const noexcept
	{
		return detail::pointerPresent(function_);
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return available();
	}

	// Precondition: engaged, with bind/reset externally serialized with calls.
	R invoke(Args... args) const noexcept
	{
		return function_(std::forward<Args>(args)...);
	}

private:
	Function function_ = nullptr;
};

} // namespace telemetry
#endif
