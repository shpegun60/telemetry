/**
 * @file TelemetryCallable.h
 * @brief Internal signature traits and native-value adapters for factories.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_DETAIL_CALLABLE_H
#define TELEMETRY_DETAIL_CALLABLE_H

#include "../field/TelemetryEnum.h"
#include "../slot/TelemetryOwnerSlot.h"
#include "../slot/TelemetryFunctionSlot.h"
#include "TelemetryOwner.h"
#include "TelemetryTarget.h"
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry {
namespace detail {

struct NoOwner {};

// A direct object (including NoOwner for a free function) adds no load or test.
// Slot adapters resolve once, test the result, and keep that object for the call.
template <class Owner>
TELEMETRY_FORCE_INLINE constexpr auto resolveFactoryOwner(Owner* owner) noexcept
{
    if constexpr (isOwnerSlot<Owner>) return owner->get();
    else return owner;
}
template <class> inline constexpr bool dependentFalse = false;
template <class T> struct IsContextCallableSlot : std::false_type {};
template <class S> struct IsContextCallableSlot<ContextFunctionSlot<S>> : std::true_type {};
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
inline constexpr bool hasFactoryCallSignature = HasConcreteCallOperator<T>::value || isCallableSlot<T>;

template <class T, bool = isCallableSlot<T>>
struct CallableObjectSignature : CallableTraits<decltype(&std::remove_cv_t<T>::operator())> {};
template <class T>
struct CallableObjectSignature<T, true> : CallableTraits<typename std::remove_cv_t<T>::Signature*> {};
template <class T>
using CallableObjectTraits = CallableObjectSignature<T>;

// Ordinary closures retain their exact address and direct operator() call.
// Explicit slots return their signature-specific snapshot/view. Callers check
// it once, then invoke that target after validation. No runtime kind tag exists.
template <class Callable>
TELEMETRY_FORCE_INLINE constexpr auto resolveFactoryCallable(Callable* callable) noexcept
{
    if constexpr (isCallableSlot<Callable>) return callable->get();
    else return callable;
}

template <class Target, class... Args>
TELEMETRY_FORCE_INLINE decltype(auto) invokeResolvedCallable(Target& target, Args&&... args) noexcept
{
    if constexpr (std::is_pointer_v<Target>) {
        static_assert(std::is_nothrow_invocable_v<decltype(*target), Args...>,
                      "Borrowed callable must be noexcept and match its signature");
        return (*target)(std::forward<Args>(args)...);
    } else return target.invoke(std::forward<Args>(args)...);
}

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

// C++17 cannot distinguish fixed from unfixed unscoped enums. A named-code
// interval is safe for both. Retain the selected dictionary in the template
// type: scanning again would lose explicit enumSpec codes outside magic_enum's
// automatic range. Gaps inside the interval remain valid numeric values.
template <class T, class Constraint, class Number>
TELEMETRY_FORCE_INLINE bool acceptsFactoryEnum(Number value) noexcept
{
    if constexpr (std::is_enum_v<T> && std::is_convertible_v<T, int>) {
        constexpr auto bounds = enumConstraintBounds<T, Constraint>();
        return value >= bounds.minimum && value <= bounds.maximum;
    } else return true;
}

// Share numeric conversion across callbacks of the same native/constraint type.
// Only the cold invocation below owns this helper's output storage.
template <class T, class Constraint>
TELEMETRY_NOINLINE bool extractConvertedFactoryValue(const Scalar& value, T& result) noexcept
{
    using Raw = RawNumberT<T>;
    const auto number = convertScalar<Raw>(value);
    if (!number || !acceptsFactoryEnum<T, Constraint>(*number)) return false;
    result = static_cast<T>(*number);
    return true;
}

// A manual Field can reuse a native setter with a different declared type.
// Match the ordinary function-pointer setter: convert only when the incoming
// alternative differs, then apply the enum cast's safety interval. This does
// not apply Field limits; callers use Field::write for that policy.
// Keep conversion storage and invocation together in the out-of-line fallback:
// the matching-tag path needs no address-taken temporary. State is the already
// resolved owner or callable snapshot, never a slot that could be read again.
template <class T, class Constraint, class Invoke, class... State>
TELEMETRY_NOINLINE auto invokeConvertedFactoryValue(const Scalar& value, Invoke invoke,
                                                   State... state) noexcept
    -> decltype(invoke(T{}, state...))
{
    using Result = decltype(invoke(T{}, state...));
    T native{};
    if (!extractConvertedFactoryValue<T, Constraint>(value, native)) return Result::InvalidValue;
    return invoke(native, state...);
}

template <class T, class Constraint, class Invoke, class... State>
TELEMETRY_FORCE_INLINE auto invokeFactoryValue(const Scalar& value, Invoke invoke,
                                             State... state) noexcept
    -> decltype(invoke(T{}, state...))
{
    using Result = decltype(invoke(T{}, state...));
    constexpr auto tag = Scalar::from(RawNumberT<T>{}).type();
    using Stored = Scalar::NativeType<tag>;
    const auto* number = value.template getIf<Stored>();
    if (number == nullptr) return invokeConvertedFactoryValue<T, Constraint>(value, invoke, state...);
    // Scoped enums have a fixed underlying type, so every representable raw
    // value can be cast safely. Unscoped enums may be unfixed: protect direct
    // Setter calls too. Field::write already applies the descriptor interval.
    if (!acceptsFactoryEnum<T, Constraint>(*number)) return Result::InvalidValue;
    return invoke(static_cast<T>(*number), state...);
}

template <auto Function, class Owner, class... A>
TELEMETRY_FORCE_INLINE auto invokeFactory(Owner* owner, A... args) noexcept
{
    static_assert(nonNullTarget<Function>, "Factory target cannot be null");
    if constexpr (CallableTraits<decltype(Function)>::member) {
        static_assert(isDirectMemberOwner<decltype(Function), Owner>,
                      "Factory owner must be the actual object or a derived object; dereference pointers explicitly or use OwnerSlot");
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
