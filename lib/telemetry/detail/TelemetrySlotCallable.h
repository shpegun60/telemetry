/**
 * @file TelemetrySlotCallable.h
 * @brief Reject implicit argument/result narrowing hidden inside a slot target.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_SLOT_CALLABLE_H
#define TELEMETRY_SLOT_CALLABLE_H
#include "../../delegate/tiny_delegate.hpp"
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

template <class F, class R, class... A>
inline constexpr bool slotSignatureMatches = [] {
    using T = std::decay_t<F>;
    if constexpr ((std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>)
                  || std::is_member_function_pointer_v<T>)
        return SlotSignaturesMatch<R(A...), tiny::sig_of_t<T>>::value;
    else if constexpr (SlotConcreteCall<T>::valid)
        return SlotSignaturesMatch<R(A...), typename SlotConcreteCall<T>::Signature>::value;
    else if constexpr (SlotGenericCall<T, std::tuple<A...>>::valid && std::is_invocable_v<F&, A...>)
        return SlotSignaturesMatch<R(A...), typename SlotGenericCall<T, std::tuple<A...>>::Signature>::value
            && SlotSignaturesMatch<R(A...), std::invoke_result_t<F&, A...>(A...)>::value;
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
} // namespace telemetry::detail
#endif
