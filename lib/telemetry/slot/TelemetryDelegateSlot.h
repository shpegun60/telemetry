/**
 * @file TelemetryDelegateSlot.h
 * @brief Stable slot owning a noexcept callable in fixed inline storage.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DELEGATE_SLOT_H
#define TELEMETRY_DELEGATE_SLOT_H
#include "TelemetrySlotTraits.h"
#include "../detail/TelemetrySlotCallable.h"
#include <utility>
namespace telemetry {
template <class S, std::size_t Bytes = 32, std::size_t Align = alignof(std::max_align_t)>
class DelegateSlot {
    static_assert(!std::is_same_v<S, S>, "DelegateSlot requires an R(Args...) noexcept function signature");
};
template <class R, class... Args, std::size_t Bytes, std::size_t Align>
class DelegateSlot<R(Args...) noexcept, Bytes, Align> {
    using Delegate = tiny::delegate<R(Args...), Bytes, Align>;
    // Owning a wrapper prevents tiny::delegate's function-pointer-conversion
    // heuristic from dropping callable state or invoking a throwing conversion.
    template <class F> struct OwnedTarget {
        F callable;
        R operator()(Args... args) noexcept
        {
            if constexpr (std::is_void_v<R>) std::invoke(callable, std::forward<Args>(args)...);
            else return std::invoke(callable, std::forward<Args>(args)...);
        }
    };
public:
    using Signature = R(Args...) noexcept;
    using Function = R (*)(Args...) noexcept;
    struct Target {
        const Delegate* delegate;
        explicit operator bool() const noexcept { return bool(*delegate); }
        R invoke(Args... args) const noexcept { return (*delegate)(std::forward<Args>(args)...); }
    };
    constexpr DelegateSlot() noexcept = default;
    DelegateSlot(const DelegateSlot&) = delete;
    DelegateSlot(DelegateSlot&&) = delete;
    DelegateSlot& operator=(const DelegateSlot&) = delete;
    DelegateSlot& operator=(DelegateSlot&&) = delete;
    template <class F> void bind(F&& callable) noexcept
    {
        using Stored = std::decay_t<F>;
        using TargetType = OwnedTarget<Stored>;
        static_assert(detail::slotSignatureMatches<Stored, R, Args...>, "DelegateSlot target signature must match exactly");
        static_assert(std::is_nothrow_invocable_r_v<R, Stored&, Args...>,
                      "DelegateSlot callable must be noexcept and match its signature");
        // Check the original callable before OwnedTarget gives it the exact R
        // return spelling; otherwise a value could masquerade as a reference.
        static_assert(tiny::detail::safely_invocable_r_v<R, Stored&, Args...>,
                      "DelegateSlot reference result requires a compatible reference");
        static_assert(std::is_nothrow_constructible_v<Stored, F&&>
                      && std::is_nothrow_move_constructible_v<Stored>
                      && std::is_nothrow_destructible_v<Stored>,
                      "DelegateSlot target construction, move and destruction must be noexcept");
        static_assert(Delegate::template fits_inline<TargetType>(),
                      "DelegateSlot target exceeds its inline size or alignment");
        if constexpr (std::is_pointer_v<std::remove_reference_t<F>> || std::is_member_pointer_v<Stored>) {
            if (callable == nullptr) { reset(); return; }
        }
        delegate_ = TargetType{std::forward<F>(callable)};
    }
    void bind(std::nullptr_t) noexcept { reset(); }
    void reset() noexcept { delegate_.reset(); }
    [[nodiscard]] Target get() const noexcept { return {std::addressof(delegate_)}; }
    [[nodiscard]] bool available() const noexcept { return bool(delegate_); }
    [[nodiscard]] explicit operator bool() const noexcept { return available(); }
    // Precondition: engaged. Calls and replacement/destruction must not overlap,
    // including reset/rebind from inside the currently executing owned callback.
    // Owning a closure does not extend lifetimes of its reference captures.
    R invoke(Args... args) const noexcept { return delegate_(std::forward<Args>(args)...); }
private:
    Delegate delegate_{};
};
} // namespace telemetry
#endif
