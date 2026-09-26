/**
 * @file TelemetrySlotCallable.h
 * @brief Reject implicit argument/result narrowing hidden inside a slot target.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_SLOT_CALLABLE_H
#define TELEMETRY_SLOT_CALLABLE_H
#include "../../delegate/tiny_delegate.hpp"
#include <functional>
#include <tuple>
#include <type_traits>
namespace telemetry::detail {
template <class T, class = void> struct SlotConcreteCall { static constexpr bool valid = false; };
template <class T> struct SlotConcreteCall<T, std::void_t<decltype(&T::operator())>> {
    static constexpr bool valid = true;
    using Signature = tiny::sig_of_t<decltype(&T::operator())>;
};
template <class T, class Args, class = void> struct SlotGenericCall { static constexpr bool valid = false; };
template <class T, class... A>
struct SlotGenericCall<T, std::tuple<A...>, std::void_t<decltype(&T::template operator()<A...>)>> {
    static constexpr bool valid = true;
    using Signature = tiny::sig_of_t<decltype(&T::template operator()<A...>)>;
};
template <class Wanted, class Actual> struct SlotSignaturesMatch : std::false_type {};
template <class Pointer, class T, class = void> struct SlotHasCall : std::false_type {};
template <class Pointer, class T>
struct SlotHasCall<Pointer, T, std::void_t<decltype(static_cast<Pointer>(&T::operator()))>> : std::true_type {};
template <class R, class... A, class S, class... B>
struct SlotSignaturesMatch<R(A...), S(B...)> : std::bool_constant<
    std::is_same_v<std::tuple<A...>, std::tuple<B...>>
    && (std::is_same_v<R, S> || std::is_void_v<R>
        || (std::is_reference_v<R> && std::is_reference_v<S>
            && std::is_convertible_v<std::add_pointer_t<std::remove_reference_t<S>>,
                                     std::add_pointer_t<std::remove_reference_t<R>>>))> {};

// Explicit operator()<int&> also makes `auto value` look like int&. Inspect
// its unreferenced specialization as well: a reference slot may only bind a
// parameter that actually declares a reference (auto& or auto&&).
template <class Wanted, class Actual> struct SlotPreservesReferences : std::false_type {};
template <class R, class... A, class S, class... B>
struct SlotPreservesReferences<R(A...), S(B...)> {
    static constexpr bool value = [] {
        if constexpr (sizeof...(A) != sizeof...(B)) return false;
        else return ((!std::is_reference_v<A> || std::is_reference_v<B>) && ...);
    }();
};

template <class F, class R, class... A>
inline constexpr bool slotSignatureMatches = [] {
    using T = std::decay_t<F>;
    if constexpr ((std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>)
                  || std::is_member_function_pointer_v<T>)
        return SlotSignaturesMatch<R(A...), tiny::sig_of_t<T>>::value;
    else if constexpr (SlotConcreteCall<T>::valid)
        return SlotSignaturesMatch<R(A...), typename SlotConcreteCall<T>::Signature>::value;
    else if constexpr (SlotGenericCall<T, std::tuple<A...>>::valid && std::is_invocable_v<F&, A...>) {
        using Plain = SlotGenericCall<T, std::tuple<std::remove_reference_t<A>...>>;
        if constexpr (!Plain::valid) return false;
        else return SlotSignaturesMatch<R(A...), typename SlotGenericCall<T, std::tuple<A...>>::Signature>::value
            && SlotPreservesReferences<R(A...), typename Plain::Signature>::value
            && std::is_nothrow_invocable_v<decltype(&T::template operator()<A...>), F&, A...>
            && SlotSignaturesMatch<R(A...), std::invoke_result_t<F&, A...>(A...)>::value;
    }
    else if constexpr (std::is_class_v<T> && std::is_invocable_v<F&, A...>) {
        // A known slot signature can select an exact overload, including a
        // generic call operator. Also check the actually selected result: a
        // different cv-qualified overload must not hide a numeric conversion.
        return (SlotHasCall<R(T::*)(A...) noexcept, T>::value
            || SlotHasCall<R(T::*)(A...) const noexcept, T>::value
            || SlotHasCall<R(T::*)(A...) & noexcept, T>::value
            || SlotHasCall<R(T::*)(A...) const & noexcept, T>::value)
            && SlotSignaturesMatch<R(A...), std::invoke_result_t<F&, A...>(A...)>::value;
    } else return false;
}();

// Invoke the exact specialization/overload that was checked above. Repeating
// normal overload resolution could choose a different by-value overload.
template <class R, class... A, class F>
decltype(auto) invokeSlotCallable(F& callable, A&&... args) noexcept
{
    using T = std::remove_cv_t<F>;
    if constexpr (SlotConcreteCall<T>::valid || !std::is_class_v<T>) {
        return std::invoke(callable, std::forward<A>(args)...);
    } else if constexpr (SlotGenericCall<T, std::tuple<A...>>::valid) {
        return std::invoke(&T::template operator()<A...>, callable, std::forward<A>(args)...);
    } else if constexpr (!std::is_const_v<F> && SlotHasCall<R(T::*)(A...) noexcept, T>::value) {
        return std::invoke(static_cast<R(T::*)(A...) noexcept>(&T::operator()), callable, std::forward<A>(args)...);
    } else if constexpr (SlotHasCall<R(T::*)(A...) const noexcept, T>::value) {
        return std::invoke(static_cast<R(T::*)(A...) const noexcept>(&T::operator()), callable, std::forward<A>(args)...);
    } else if constexpr (!std::is_const_v<F> && SlotHasCall<R(T::*)(A...) & noexcept, T>::value) {
        return std::invoke(static_cast<R(T::*)(A...) & noexcept>(&T::operator()), callable, std::forward<A>(args)...);
    } else {
        return std::invoke(static_cast<R(T::*)(A...) const & noexcept>(&T::operator()), callable, std::forward<A>(args)...);
    }
}
} // namespace telemetry::detail
#endif
