/**
 * @file TelemetryCallable.h
 * @brief Internal signature traits and native-value adapters for factories.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DETAIL_CALLABLE_H
#define TELEMETRY_DETAIL_CALLABLE_H

#include "../field/TelemetryEnum.h"
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry {
namespace detail {

struct NoOwner {};
template <class> inline constexpr bool dependentFalse = false;
template <class T> struct CallableTraits {
    static_assert(dependentFalse<T>, "Factory requires a noexcept free/static function or member function");
};

template <class R, class... A>
struct CallableTraits<R (*)(A...) noexcept> {
    using Result = R;
    using Arguments = std::tuple<A...>;
    static constexpr bool member = false;
    static constexpr std::size_t arity = sizeof...(A);
};
template <class R, class C, class... A>
struct CallableTraits<R (C::*)(A...) noexcept> : CallableTraits<R (*)(A...) noexcept> {
    using Class = C;
    static constexpr bool member = true;
};
template <class R, class C, class... A>
struct CallableTraits<R (C::*)(A...) const noexcept> : CallableTraits<R (C::*)(A...) noexcept> {};
template <class R, class C, class... A>
struct CallableTraits<R (C::*)(A...) & noexcept> : CallableTraits<R (C::*)(A...) noexcept> {};
template <class R, class C, class... A>
struct CallableTraits<R (C::*)(A...) const & noexcept> : CallableTraits<R (C::*)(A...) noexcept> {};

// A borrowed callable must expose one concrete, noexcept operator(). Generic
// and overloaded call operators have no single address and are rejected.
template <class T, class = void>
struct HasConcreteCallOperator : std::false_type {};
template <class T>
struct HasConcreteCallOperator<T,
    std::void_t<decltype(&std::remove_cv_t<T>::operator())>> : std::true_type {};

template <class T>
using CallableObjectTraits = CallableTraits<decltype(&std::remove_cv_t<T>::operator())>;

// Capture-free lambdas expose the built-in unary-plus conversion to an exact
// function pointer. Keep those on the existing direct-function path; class
// callables without that conversion must be borrowed from a stable lvalue.
template <class T, class = void>
struct HasNativeFunctionPointer : std::false_type {};
template <class T>
struct HasNativeFunctionPointer<T, std::void_t<decltype(+std::declval<T&>())>>
    : std::bool_constant<std::is_pointer_v<decltype(+std::declval<T&>())>
        && std::is_function_v<std::remove_pointer_t<decltype(+std::declval<T&>())>>
        && std::is_convertible_v<T&, decltype(+std::declval<T&>())>> {};

template <class T, bool = std::is_enum_v<T>> struct RawNumber { using Type = T; };
template <class T> struct RawNumber<T, true> { using Type = std::underlying_type_t<T>; };
template <class T> using RawNumberT = typename RawNumber<T>::Type;

template <class T>
constexpr FieldType inferredType() noexcept
{
    static_assert(isFactoryValue<T>, "Factory values must be numeric or enum values, without references");
    if constexpr (std::is_enum_v<T>) return enumType<T>();
    else return numericType<T>();
}

// Extraction follows normalization. Check direct Setter use as well, without
// std::get on a wrong alternative. Enum extrema also protect unscoped enums.
template <class T>
TELEMETRY_FORCE_INLINE bool extractFactoryValue(const Scalar& value, T& result) noexcept
{
    constexpr auto tag = Scalar::from(RawNumberT<T>{}).type();
    using Stored = Scalar::NativeType<tag>;
    const auto* number = value.template getIf<Stored>();
    if (number == nullptr) return false;
    // Scoped enums have a fixed underlying type, so every representable raw
    // value can be cast safely. Unscoped enums may be unfixed: protect direct
    // Setter calls too. Field::write already applies the descriptor interval.
    if constexpr (std::is_enum_v<T> && std::is_convertible_v<T, int>) {
        constexpr auto type = inferredType<T>();
        if (*number < type.minimum().template get<Stored>()
            || *number > type.maximum().template get<Stored>()) return false;
    }
    result = static_cast<T>(*number);
    return true;
}

template <auto Function, class Owner, class... A>
TELEMETRY_FORCE_INLINE auto invokeFactory(Owner* owner, A... args) noexcept
{
    static_assert(Function != nullptr, "Factory target cannot be null");
    if constexpr (CallableTraits<decltype(Function)>::member) {
        static_assert(std::is_nothrow_invocable_v<decltype(Function), Owner&, A...>,
                      "Factory owner or parameter types do not match the noexcept target");
        if constexpr (std::is_same_v<std::remove_cv_t<Owner>,
                                     typename CallableTraits<decltype(Function)>::Class>) {
            return ((*owner).*Function)(args...);
        } else {
            // Standard INVOKE handles base adjustment without GCC's warning
            // for a member pointer applied directly to a derived object.
            return std::invoke(Function, *owner, args...);
        }
    } else {
        static_assert(std::is_nothrow_invocable_v<decltype(Function), A...>,
                      "Factory parameter types do not match the noexcept target");
        return Function(args...);
    }
}

} // namespace detail
} // namespace telemetry
#endif
