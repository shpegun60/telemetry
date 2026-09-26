/**
 * @file TelemetryContextFunctionSlot.h
 * @brief Rebindable noexcept function plus a borrowed void pointer context.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_CONTEXT_FUNCTION_SLOT_H
#define TELEMETRY_CONTEXT_FUNCTION_SLOT_H
#include "TelemetrySlotTraits.h"
#include "../core/TelemetryCompiler.h"
#include "../detail/TelemetryTarget.h"
#include <utility>
namespace telemetry {
template <class S> class ContextFunctionSlot {
    static_assert(!std::is_same_v<S, S>, "ContextFunctionSlot requires an R(Args...) noexcept function signature");
};
template <class R, class... Args>
class ContextFunctionSlot<R(Args...) noexcept> {
public:
    using Signature = R(Args...) noexcept;
    using Function = R (*)(void*, Args...) noexcept;
    // A by-value snapshot keeps the selected function/context pair together.
    // The context may be null if the callback supports it; only fn is presence.
    struct Target {
        Function function = nullptr;
        void* context = nullptr;
        constexpr explicit operator bool() const noexcept { return detail::pointerPresent(function); }
        TELEMETRY_FORCE_INLINE R invoke(Args... args) const noexcept { return function(context, std::forward<Args>(args)...); }
    };
    constexpr ContextFunctionSlot() noexcept = default;
    ContextFunctionSlot(const ContextFunctionSlot&) = delete;
    ContextFunctionSlot(ContextFunctionSlot&&) = delete;
    ContextFunctionSlot& operator=(const ContextFunctionSlot&) = delete;
    ContextFunctionSlot& operator=(ContextFunctionSlot&&) = delete;
    constexpr void bind(Function function, void* context) noexcept { target_ = {function, context}; }
    constexpr void reset() noexcept { target_ = {}; }
    [[nodiscard]] constexpr Target get() const noexcept { return target_; }
    [[nodiscard]] constexpr bool available() const noexcept { return bool(target_); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return available(); }
    // Precondition: engaged. The slot/context must outlive every active call;
    // bind/reset and access must be externally serialized.
    R invoke(Args... args) const noexcept { return target_.invoke(std::forward<Args>(args)...); }
private:
    Target target_{};
};
} // namespace telemetry
#endif
