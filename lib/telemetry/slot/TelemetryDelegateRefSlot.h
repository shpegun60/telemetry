/**
 * @file TelemetryDelegateRefSlot.h
 * @brief Stable noexcept slot borrowing a method, function or callable object.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DELEGATE_REF_SLOT_H
#define TELEMETRY_DELEGATE_REF_SLOT_H
#include "TelemetrySlotTraits.h"
#include "../core/TelemetryCompiler.h"
#include "../detail/TelemetrySlotCallable.h"
#include "../detail/TelemetryOwner.h"
#include <utility>
namespace telemetry {
template <class S> class DelegateRefSlot {
    static_assert(!std::is_same_v<S, S>, "DelegateRefSlot requires an R(Args...) noexcept function signature");
};
template <class R, class... Args>
class DelegateRefSlot<R(Args...) noexcept> {
    using Delegate = tiny::delegate_ref<R(Args...)>;
public:
    using Signature = R(Args...) noexcept;
    using Function = R (*)(Args...) noexcept;
    class Target {
        friend class DelegateRefSlot;
        const Delegate* delegate;
        constexpr explicit Target(const Delegate& value) noexcept : delegate(std::addressof(value)) {}
    public:
        Target() = delete;
        constexpr explicit operator bool() const noexcept { return bool(*delegate); }
        TELEMETRY_FORCE_INLINE R invoke(Args... args) const noexcept
        {
            return delegate->call_or([](Args...) noexcept -> R { tiny::detail::trap(); },
                                     std::forward<Args>(args)...);
        }
    };
    constexpr DelegateRefSlot() noexcept = default;
    DelegateRefSlot(const DelegateRefSlot&) = delete;
    DelegateRefSlot(DelegateRefSlot&&) = delete;
    DelegateRefSlot& operator=(const DelegateRefSlot&) = delete;
    DelegateRefSlot& operator=(DelegateRefSlot&&) = delete;
    void bind(Function function) noexcept
    {
        if (function) delegate_ = function;
        else reset();
    }
    template <class F, std::enable_if_t<std::is_class_v<std::remove_cv_t<F>>, int> = 0>
    void bind(F& callable) noexcept
    {
        static_assert(detail::slotSignatureMatches<F, R, Args...>, "DelegateRefSlot target signature must match exactly");
        static_assert(std::is_nothrow_invocable_r_v<R, F&, Args...>, "DelegateRefSlot callable must be noexcept and match its signature");
        delegate_ = Delegate::template bind_context<&invokeCallable_<F>>(callable);
    }
    template <class F = void, class Argument,
              std::enable_if_t<!detail::isBorrowedObjectArgument<F, Argument>
                  && !std::is_convertible_v<Argument, Function>, int> = 0>
    void bind(Argument&&) = delete;
    template <auto Method, class Owner, std::enable_if_t<!std::is_reference_v<Owner>, int> = 0>
    constexpr void bind(Owner& owner) noexcept
    {
        static_assert(detail::slotSignatureMatches<decltype(Method), R, Args...>, "DelegateRefSlot target signature must match exactly");
        static_assert(detail::isDirectMemberOwner<decltype(Method), Owner>, "DelegateRefSlot requires a direct owner object");
        static_assert(std::is_nothrow_invocable_r_v<R, decltype(Method), Owner&, Args...>,
                      "DelegateRefSlot method must be noexcept and match its signature");
        delegate_ = Delegate::template bind<Method>(owner);
    }
    template <auto Method, class Owner = void, class Argument,
              std::enable_if_t<!detail::isBorrowedObjectArgument<Owner, Argument>, int> = 0>
    void bind(Argument&&) = delete;
    template <auto FunctionTarget> constexpr void bind() noexcept
    {
        static_assert(detail::slotSignatureMatches<decltype(FunctionTarget), R, Args...>, "DelegateRefSlot target signature must match exactly");
        static_assert(std::is_nothrow_invocable_r_v<R, decltype(FunctionTarget), Args...>,
                      "DelegateRefSlot function must be noexcept and match its signature");
        delegate_ = Delegate::template bind<FunctionTarget>();
    }
    constexpr void reset() noexcept { delegate_.reset(); }
    [[nodiscard]] constexpr Target get() const noexcept { return Target(delegate_); }
    [[nodiscard]] constexpr bool available() const noexcept { return bool(delegate_); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return available(); }
    // Precondition: engaged. The external callable/owner and everything it
    // borrows must outlive calls. Rebinding never owns or destroys that target.
    R invoke(Args... args) const noexcept
    {
        return get().invoke(std::forward<Args>(args)...);
    }
private:
    template <class F>
    static R invokeCallable_(F& callable, Args... args) noexcept
    {
        if constexpr (std::is_void_v<R>) detail::invokeSlotCallable<R, Args...>(callable, std::forward<Args>(args)...);
        else return detail::invokeSlotCallable<R, Args...>(callable, std::forward<Args>(args)...);
    }
    Delegate delegate_{};
};
} // namespace telemetry
#endif
