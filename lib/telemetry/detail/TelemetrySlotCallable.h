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

// Borrow a named class callable without considering its function-pointer
// conversion. A capture-free generic lambda may instantiate an unrelated
// operator() specialization merely while that conversion is considered.
template <class Explicit, class Argument, class Function>
inline constexpr bool isSlotFunctionArgument = [] {
    if constexpr (!std::is_void_v<Explicit>
                  || (std::is_lvalue_reference_v<Argument>
                      && std::is_class_v<std::remove_reference_t<Argument>>)) return false;
    else return std::is_convertible_v<Argument, Function>;
}();

template <class T> using SlotValue = std::remove_cv_t<std::remove_reference_t<T>>;
template <class Wanted, class Actual>
inline constexpr bool slotArgumentMatches = std::is_same_v<Wanted, Actual>
    || (std::is_reference_v<Wanted> && std::is_same_v<SlotValue<Wanted>, SlotValue<Actual>>
        && ((std::is_reference_v<Actual> && std::is_convertible_v<Wanted, Actual>)
            || (!std::is_reference_v<Actual> && std::is_const_v<std::remove_reference_t<Wanted>>)));

template <class Wanted, class Actual> struct SlotSignaturesMatch : std::false_type {};
template <class R, class... A, class S, class... B>
struct SlotSignaturesMatch<R(A...), S(B...)> {
    static constexpr bool value = [] {
        if constexpr (sizeof...(A) != sizeof...(B)) return false;
        else return (slotArgumentMatches<A, B> && ...)
            && (std::is_same_v<R, S> || std::is_void_v<R>
                || (std::is_reference_v<R> && std::is_reference_v<S>
                    && std::is_convertible_v<std::add_pointer_t<std::remove_reference_t<S>>,
                                             std::add_pointer_t<std::remove_reference_t<R>>>));
    }();
};

// Deduce template arguments from a function type, never from an explicit
// operator()<A...>. For example, const auto& cannot match an int& parameter;
// auto&& can, with exactly the specialization used by an ordinary call.
template <class Pointer, class T, class = void> struct SlotHasCall : std::false_type {};
template <class Pointer, class T>
struct SlotHasCall<Pointer, T, std::void_t<decltype(static_cast<Pointer>(&T::operator()))>> : std::true_type {};
template <class Pointer, class T, class = void> struct SlotHasTemplateCall : std::false_type {};
template <class Pointer, class T>
struct SlotHasTemplateCall<Pointer, T,
    std::void_t<decltype(static_cast<Pointer>(&T::template operator()<>))>> : std::true_type {};

template <class Pointer, class T>
inline constexpr bool slotNonTemplateCall = [] {
    if constexpr (!SlotHasCall<Pointer, T>::value) return false;
    else if constexpr (!SlotHasTemplateCall<Pointer, T>::value) return true;
    else return static_cast<Pointer>(&T::operator())
             != static_cast<Pointer>(&T::template operator()<>);
}();

template <class F, class Signature> struct SlotCall;
template <class F, class R, class... A>
struct SlotCall<F, R(A...)> {
    using T = std::remove_cv_t<F>;
    using Mutable = R(T::*)(A...) noexcept;
    using MutableRef = R(T::*)(A...) & noexcept;
    using Const = R(T::*)(A...) const noexcept;
    using ConstRef = R(T::*)(A...) const & noexcept;
    // The implicit object argument also participates in overload resolution.
    static constexpr int rank = [] {
        if constexpr (!std::is_const_v<F>
                      && (SlotHasCall<Mutable, T>::value || SlotHasCall<MutableRef, T>::value)) return 2;
        else if constexpr (SlotHasCall<Const, T>::value || SlotHasCall<ConstRef, T>::value) return 1;
        else return 0;
    }();
    static constexpr bool nonTemplate = [] {
        if constexpr (rank == 2)
            return slotNonTemplateCall<Mutable, T> || slotNonTemplateCall<MutableRef, T>;
        else if constexpr (rank == 1)
            return slotNonTemplateCall<Const, T> || slotNonTemplateCall<ConstRef, T>;
        else return false;
    }();
};

// A by-value template can also match a reference function type by deducing
// T=int&. Look for value forms before accepting it. Parameters can share a
// template argument (T, T, int&), so check combinations as well as each input.
template <class F, class R, class Selected, class Prefix, class Wanted,
          class Actual, bool Changed = false> struct SlotPreservesInputs;
template <class F, class R, class Selected, class... B, bool Changed>
struct SlotPreservesInputs<F, R, Selected, std::tuple<B...>, std::tuple<>, std::tuple<>, Changed> {
    static constexpr bool value = [] {
        if constexpr (!Changed) return true;
        else {
            using Copy = SlotCall<F, R(B...)>;
            if constexpr (Copy::rank == 0) return true;
            else if constexpr (Copy::rank > Selected::rank || !Selected::nonTemplate) return false;
            else return Copy::rank < Selected::rank || !Copy::nonTemplate;
        }
    }();
};
template <class F, class R, class Selected, class... Prefix, class A, class... RestA,
          class B, class... RestB, bool Changed>
struct SlotPreservesInputs<F, R, Selected, std::tuple<Prefix...>, std::tuple<A, RestA...>,
                          std::tuple<B, RestB...>, Changed> {
    template <class Next, bool Copied>
    using Try = SlotPreservesInputs<F, R, Selected, std::tuple<Prefix..., Next>,
                                   std::tuple<RestA...>, std::tuple<RestB...>, Changed || Copied>;
    static constexpr bool value = [] {
        if constexpr (!Try<B, false>::value) return false;
        else if constexpr (std::is_reference_v<A> && !std::is_const_v<std::remove_reference_t<A>>)
            return Try<SlotValue<B>, true>::value;
        else return true;
    }();
};

template <class F, class R, class Wanted, class Actual> struct SlotCandidate;
template <class F, class R, class... A, class... B>
struct SlotCandidate<F, R, std::tuple<A...>, std::tuple<B...>> {
    using Selected = SlotCall<F, R(B...)>;
    static constexpr bool matched = Selected::rank != 0;
    static constexpr bool value = [] {
        if constexpr (!matched) return false;
        else return SlotPreservesInputs<F, R, Selected, std::tuple<>,
                                        std::tuple<A...>, std::tuple<B...>>::value;
    }();
};

// Try only same-value-type parameter forms. Each failed function-type match
// is discarded before deducing an auto return from an unrelated lambda body.
template <class F, class R, class Wanted, class Prefix, class Remaining> struct SlotParameters;
template <class F, class R, class Wanted, class... B>
struct SlotParameters<F, R, Wanted, std::tuple<B...>, std::tuple<>>
    : SlotCandidate<F, R, Wanted, std::tuple<B...>> {};
template <class F, class R, class Wanted, class... B, class A, class... Rest>
struct SlotParameters<F, R, Wanted, std::tuple<B...>, std::tuple<A, Rest...>> {
    template <class Next>
    using Try = SlotParameters<F, R, Wanted, std::tuple<B..., Next>, std::tuple<Rest...>>;
    static auto referenceForms()
    {
        if constexpr (Try<A>::matched) return Try<A>{};
        else if constexpr (std::is_reference_v<A>) {
            using ConstValue = std::add_const_t<std::remove_reference_t<A>>;
            if constexpr (std::is_rvalue_reference_v<A>) {
                if constexpr (Try<ConstValue&&>::matched) return Try<ConstValue&&>{};
                else return Try<ConstValue&>{};
            } else return Try<ConstValue&>{};
        } else return Try<A>{};
    }
    static auto choose()
    {
        // A read-only input permits a copy. Try that ordinary deduction first:
        // operator()<const int&> would give an auto-by-value lambda a reference.
        if constexpr (std::is_reference_v<A> && std::is_const_v<std::remove_reference_t<A>>) {
            if constexpr (Try<SlotValue<A>>::matched) return Try<SlotValue<A>>{};
            else return referenceForms();
        } else return referenceForms();
    }
    using Choice = decltype(choose());
    static constexpr bool matched = Choice::matched;
    static constexpr bool value = Choice::value;
};

template <class F, class R, class... A>
inline constexpr bool slotSignatureMatches = [] {
    using T = std::decay_t<F>;
    if constexpr ((std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>)
                  || std::is_member_function_pointer_v<T>)
        return SlotSignaturesMatch<R(A...), tiny::sig_of_t<T>>::value;
    else if constexpr (!std::is_invocable_v<F&, A...>) return false;
    else if constexpr (SlotConcreteCall<T>::valid)
        return SlotSignaturesMatch<R(A...), typename SlotConcreteCall<T>::Signature>::value;
    else if constexpr (std::is_class_v<T>) {
        using Result = std::invoke_result_t<F&, A...>;
        return SlotSignaturesMatch<R(A...), Result(A...)>::value
            && SlotParameters<F, Result, std::tuple<A...>, std::tuple<>, std::tuple<A...>>::value;
    } else return false;
}();

// Always use the same ordinary overload resolution that bind checks for
// invocability, noexcept and result lifetime. Do not force a specialization.
template <class R, class... A, class F>
decltype(auto) invokeSlotCallable(F& callable, A&&... args) noexcept
{
    return std::invoke(callable, std::forward<A>(args)...);
}
} // namespace telemetry::detail
#endif
