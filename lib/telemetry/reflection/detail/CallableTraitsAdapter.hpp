/*
 * @file CallableTraitsAdapter.hpp
 * @brief Exact C++20 signature facts, including member cv/ref qualifiers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_CALLABLE_TRAITS_ADAPTER_HPP
#define TELEMETRY_STRUCTURED_CALLABLE_TRAITS_ADAPTER_HPP

#include <telemetry/slot/TelemetrySlotTraits.h>

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>

namespace telemetry::reflection {

enum class RefQualifier { None, LValue, RValue };

namespace detail {

template <class>
inline constexpr bool unsupportedSignature = false;

template <class ResultType, class OwnerType, bool Const, bool Volatile,
          RefQualifier Ref, bool Noexcept, bool Variadic, class... Args>
struct FunctionFacts {
    using Result = ResultType;
    using Arguments = std::tuple<Args...>;
    using Owner = OwnerType;
    static constexpr std::size_t arity = sizeof...(Args);
    static constexpr bool isMember = !std::is_void_v<OwnerType>;
    static constexpr bool isConst = Const;
    static constexpr bool isVolatile = Volatile;
    static constexpr RefQualifier refQualifier = Ref;
    static constexpr bool isNoexcept = Noexcept;
    static constexpr bool isVariadic = Variadic;
};

template <class Signature>
struct SignatureFacts {
    static_assert(unsupportedSignature<Signature>,
                  "Callable requires an unambiguous function signature; "
                  "use an exact function/member pointer or an exact slot signature");
};

template <class R, class... Args>
struct SignatureFacts<R(Args...)>
    : FunctionFacts<R, void, false, false, RefQualifier::None, false, false, Args...> {};

template <class R, class... Args>
struct SignatureFacts<R(Args...) noexcept>
    : FunctionFacts<R, void, false, false, RefQualifier::None, true, false, Args...> {};

template <class R, class... Args>
struct SignatureFacts<R(Args..., ...)>
    : FunctionFacts<R, void, false, false, RefQualifier::None, false, true, Args...> {};

template <class R, class... Args>
struct SignatureFacts<R(Args..., ...) noexcept>
    : FunctionFacts<R, void, false, false, RefQualifier::None, true, true, Args...> {};

template <class F> requires std::is_function_v<F>
struct SignatureFacts<F*> : SignatureFacts<F> {};

template <class F> requires std::is_function_v<F>
struct SignatureFacts<F&> : SignatureFacts<F> {};

template <class F> requires std::is_function_v<F>
struct SignatureFacts<F&&> : SignatureFacts<F> {};

// There are twelve cv/ref combinations. Generate only their syntax here; all
// facts and public diagnostics remain in the ordinary templates above/below.
#define TELEMETRY_STRUCTURED_METHOD(CV, REF, CONST, VOLATILE, REF_KIND) \
    template <class R, class C, class... Args> \
    struct SignatureFacts<R(C::*)(Args...) CV REF> \
        : FunctionFacts<R, C, CONST, VOLATILE, REF_KIND, false, false, Args...> {}; \
    template <class R, class C, class... Args> \
    struct SignatureFacts<R(C::*)(Args...) CV REF noexcept> \
        : FunctionFacts<R, C, CONST, VOLATILE, REF_KIND, true, false, Args...> {}; \
    template <class R, class C, class... Args> \
    struct SignatureFacts<R(C::*)(Args..., ...) CV REF> \
        : FunctionFacts<R, C, CONST, VOLATILE, REF_KIND, false, true, Args...> {}; \
    template <class R, class C, class... Args> \
    struct SignatureFacts<R(C::*)(Args..., ...) CV REF noexcept> \
        : FunctionFacts<R, C, CONST, VOLATILE, REF_KIND, true, true, Args...> {};

TELEMETRY_STRUCTURED_METHOD(, , false, false, RefQualifier::None)
TELEMETRY_STRUCTURED_METHOD(, &, false, false, RefQualifier::LValue)
TELEMETRY_STRUCTURED_METHOD(, &&, false, false, RefQualifier::RValue)
TELEMETRY_STRUCTURED_METHOD(const, , true, false, RefQualifier::None)
TELEMETRY_STRUCTURED_METHOD(const, &, true, false, RefQualifier::LValue)
TELEMETRY_STRUCTURED_METHOD(const, &&, true, false, RefQualifier::RValue)
TELEMETRY_STRUCTURED_METHOD(volatile, , false, true, RefQualifier::None)
TELEMETRY_STRUCTURED_METHOD(volatile, &, false, true, RefQualifier::LValue)
TELEMETRY_STRUCTURED_METHOD(volatile, &&, false, true, RefQualifier::RValue)
TELEMETRY_STRUCTURED_METHOD(const volatile, , true, true, RefQualifier::None)
TELEMETRY_STRUCTURED_METHOD(const volatile, &, true, true, RefQualifier::LValue)
TELEMETRY_STRUCTURED_METHOD(const volatile, &&, true, true, RefQualifier::RValue)

#undef TELEMETRY_STRUCTURED_METHOD

template <class T>
struct SlotSignature { using type = T; };

template <class T>
struct SlotSignature<std::reference_wrapper<T>> { using type = T; };

template <class Signature>
struct SlotSignature<telemetry::FunctionSlot<Signature>> { using type = Signature; };

template <class Signature>
struct SlotSignature<telemetry::ContextFunctionSlot<Signature>> { using type = Signature; };

template <class Signature>
struct SlotSignature<telemetry::DelegateRefSlot<Signature>> { using type = Signature; };

template <class Signature, std::size_t Bytes, std::size_t Align>
struct SlotSignature<telemetry::DelegateSlot<Signature, Bytes, Align>> {
    using type = Signature;
};

template <class T, class = void>
struct CallableTraitsAdapter : SignatureFacts<T> {};

template <class T>
struct CallableTraitsAdapter<T, std::void_t<decltype(&T::operator())>>
    : SignatureFacts<decltype(&T::operator())> {};

template <class T>
using ResolvedFacts = CallableTraitsAdapter<
    typename SlotSignature<std::remove_cvref_t<T>>::type>;

} // namespace detail
} // namespace telemetry::reflection

#endif
