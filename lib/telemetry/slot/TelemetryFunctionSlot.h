/**
 * @file TelemetryFunctionSlot.h
 * @brief Stable slot for selecting a noexcept function at runtime.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FUNCTION_SLOT_H
#define TELEMETRY_FUNCTION_SLOT_H

#include <type_traits>
#include <utility>
#include "TelemetrySlotTraits.h"

namespace telemetry {

template <class Signature>
class FunctionSlot {
    static_assert(!std::is_same_v<Signature, Signature>,
                  "FunctionSlot requires an R(Args...) noexcept function signature");
};

// Tables borrow the slot's stable address, not a snapshot of its function.
// Keep it alive for every table access. Bind/reset and access must be externally
// serialized; this pointer provides no synchronization or callback ownership.
// Adapters check get() before invocation; no unchecked operator() is exposed.
template <class R, class... Args>
class FunctionSlot<R(Args...) noexcept> {
public:
    using Signature = R(Args...) noexcept;
    using Function = R (*)(Args...) noexcept;
    constexpr FunctionSlot() noexcept = default;
    FunctionSlot(const FunctionSlot&) = delete;
    FunctionSlot(FunctionSlot&&) = delete;
    FunctionSlot& operator=(const FunctionSlot&) = delete;
    FunctionSlot& operator=(FunctionSlot&&) = delete;

    constexpr void bind(Function function) noexcept { function_ = function; }
    constexpr void reset() noexcept { function_ = nullptr; }
    [[nodiscard]] constexpr Function get() const noexcept { return function_; }
    [[nodiscard]] constexpr bool available() const noexcept { return function_ != nullptr; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return function_ != nullptr; }
    // Precondition: engaged, with bind/reset externally serialized with calls.
    R invoke(Args... args) const noexcept { return function_(std::forward<Args>(args)...); }
private:
    Function function_ = nullptr;
};

} // namespace telemetry
#endif
