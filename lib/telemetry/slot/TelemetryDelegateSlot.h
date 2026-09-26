/**
 * @file TelemetryDelegateSlot.h
 * @brief Stable slot owning a noexcept callable in fixed inline storage.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DELEGATE_SLOT_H
#define TELEMETRY_DELEGATE_SLOT_H
#include "TelemetrySlotTraits.h"
#include "../core/TelemetryCompiler.h"
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
            if constexpr (std::is_void_v<R>) detail::invokeSlotCallable<R, Args...>(callable, std::forward<Args>(args)...);
            else return detail::invokeSlotCallable<R, Args...>(callable, std::forward<Args>(args)...);
        }
    };
public:
    using Signature = R(Args...) noexcept;
    using Function = R (*)(Args...) noexcept;
    class Target {
        friend class DelegateSlot;
        const Delegate* delegate;
        constexpr explicit Target(const Delegate& value) noexcept : delegate(std::addressof(value)) {}
    public:
        Target() = delete;
        explicit operator bool() const noexcept { return bool(*delegate); }
        TELEMETRY_FORCE_INLINE R invoke(Args... args) const noexcept
        {
            return delegate->call_or([](Args...) noexcept -> R { tiny::detail::trap(); },
                                     std::forward<Args>(args)...);
        }
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
    [[nodiscard]] Target get() const noexcept { return Target(delegate_); }
    [[nodiscard]] bool available() const noexcept { return bool(delegate_); }
    [[nodiscard]] explicit operator bool() const noexcept { return available(); }
    // Precondition: engaged. Calls and replacement/destruction must not overlap,
    // including reset/rebind from inside the currently executing owned callback.
    // Owning a closure does not extend lifetimes of its reference captures.
    R invoke(Args... args) const noexcept
    {
        return get().invoke(std::forward<Args>(args)...);
    }
private:
    Delegate delegate_{};
};
} // namespace telemetry
#endif
