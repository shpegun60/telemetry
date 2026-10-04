/*
 * @file Callable.hpp
 * @brief Signature facts and early endpoint-shape validation.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_CALLABLE_HPP
#define TELEMETRY_CALLABLE_HPP

#include "detail/CallableTraitsAdapter.hpp"

#include <tuple>
#include <type_traits>

namespace telemetry {

// The value type is defined with its storage in Stage 06. Reflection only
// needs to recognize the wrapper and its payload type.
template <class T> class ServiceResult;
template <class T> class BorrowedServiceResult;

namespace reflection {

template <class Signature>
struct Function : detail::ResolvedFacts<Signature> {};

enum class EndpointKind { Field, Command, Service };

namespace detail {

template <class T>
struct ServicePayload {
    using type = T;
    static constexpr bool wrapped = false;
    static constexpr bool borrowed = false;
};

template <class T>
struct ServicePayload<ServiceResult<T>> {
    using type = T;
    static constexpr bool wrapped = true;
    static constexpr bool borrowed = false;
};

template <class T>
struct ServicePayload<BorrowedServiceResult<T>> {
    using type = T;
    static constexpr bool wrapped = true;
    static constexpr bool borrowed = true;
};

template <class Arguments>
struct RequestShape {
    using type = void;
    static constexpr bool supported = false;
};

template <>
struct RequestShape<std::tuple<>> {
    using type = void;
    static constexpr bool supported = true;
};

template <class Arg>
struct RequestShape<std::tuple<Arg>> {
    using type = std::remove_cvref_t<Arg>;
    static constexpr bool supported =
        !std::is_rvalue_reference_v<Arg>
        && (!std::is_lvalue_reference_v<Arg>
            || std::is_const_v<std::remove_reference_t<Arg>>)
        && !std::is_volatile_v<std::remove_reference_t<Arg>>
        && !std::is_pointer_v<type>;
};

} // namespace detail

// Factory-specific rules (for example, a Field setter's WriteResult) are
// checked later. These checks reject a lossy generic endpoint shape before
// removing cv/ref from its request and unwrapping its ServiceResult.
template <EndpointKind Kind, class Signature>
struct EndpointTraits {
    using Callable = Function<Signature>;
    using Result = typename Callable::Result;
    using Arguments = typename Callable::Arguments;
    using RequestShape = detail::RequestShape<Arguments>;
    using Request = typename RequestShape::type;
    // Only a Service has a response payload. For Field/Command the Result is
    // a getter value or operation status, interpreted by its own factory.
    using Response = std::conditional_t<Kind == EndpointKind::Service,
        typename detail::ServicePayload<std::remove_cvref_t<Result>>::type, void>;

    static constexpr EndpointKind kind = Kind;
    static constexpr bool wrapsServiceResult = Kind == EndpointKind::Service
        && detail::ServicePayload<std::remove_cvref_t<Result>>::wrapped;
    static constexpr bool wrapsBorrowedServiceResult = Kind == EndpointKind::Service
        && detail::ServicePayload<std::remove_cvref_t<Result>>::borrowed;
    // A const lvalue reference declares a borrowed value. Preserve that fact
    // before cv/ref normalization; Command statuses and owning result wrappers
    // must still be returned by value.
    static constexpr bool borrowsResult = Kind != EndpointKind::Command
        && std::is_lvalue_reference_v<Result>
        && std::is_const_v<std::remove_reference_t<Result>>
        && !std::is_volatile_v<std::remove_reference_t<Result>>
        && !detail::ServicePayload<std::remove_cvref_t<Result>>::wrapped;
    static constexpr bool borrowsResponse = Kind == EndpointKind::Service
        && (borrowsResult || wrapsBorrowedServiceResult);

    static_assert(Callable::isNoexcept, "Structured endpoints must be noexcept");
    static_assert(!Callable::isVariadic, "Structured endpoints cannot be variadic");
    static_assert(!Callable::isVolatile, "Volatile owner methods are unsupported");
    static_assert(Callable::refQualifier != RefQualifier::RValue,
                  "Rvalue-qualified owner methods are unsupported");
    static_assert(Callable::arity <= 1,
                  "Use one request structure instead of multiple parameters");
    static_assert(Callable::arity > 1 || RequestShape::supported,
                  "Request must be by value or const lvalue reference, never volatile or a pointer");
    static_assert((!std::is_reference_v<Result> || borrowsResult) && !std::is_pointer_v<Result>
                  && !std::is_pointer_v<Response> && !std::is_reference_v<Response>,
                  "Response must be a value or void, or an exact const lvalue reference; pointers and other references are unsupported");
};

} // namespace reflection
} // namespace telemetry

#endif
