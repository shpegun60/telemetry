/*
 * @file Service.hpp
 * @brief Native service bindings with inferred request and response types.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_SERVICE_SERVICE_HPP
#define TELEMETRY_SERVICE_SERVICE_HPP

#include "../reflection/Callable.hpp"
#include "../detail/Binding.hpp"
#include "../detail/Name.hpp"
#include "../result/ServiceResult.hpp"
#include "../result/BorrowedServiceResult.hpp"
#include "../type/Traits.hpp"

#include <telemetry/detail/Owner.hpp>
#include <telemetry/detail/Target.hpp>
#include <telemetry/slot/TelemetryContextFunctionSlot.h>
#include <telemetry/slot/TelemetryDelegateRefSlot.h>
#include <telemetry/slot/TelemetryDelegateSlot.h>
#include <telemetry/slot/TelemetryFunctionSlot.h>
#include <telemetry/slot/TelemetryOwnerSlot.h>

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry {
template <class... Definitions>
class ServiceTable;

namespace service_detail {

template <class Signature>
struct Shape : reflection::EndpointTraits<reflection::EndpointKind::Service, Signature> {
    using Base = reflection::EndpointTraits<reflection::EndpointKind::Service, Signature>;
    using Request = typename Base::Request;
    using Response = typename Base::Response;
    using Result = typename Base::Result;
    using WrappedResult = std::conditional_t<Base::wrapsBorrowedServiceResult,
        BorrowedServiceResult<Response>, ServiceResult<Response>>;

    // A result wrapper can occur as an incomplete return type in a declaration.
    // Validate its payload here, rather than waiting for wrapper construction.
    static_assert(std::is_same_v<Response, std::remove_cvref_t<Response>>,
                  "Service response payload must be an unqualified native type");
    static_assert(!Base::wrapsBorrowedServiceResult || !std::is_void_v<Response>,
                  "Borrowed Service response must be a non-void aggregate");
    static_assert(std::is_void_v<Request> || Type<Request>::kind == TypeKind::Struct,
                  "Service request must be an aggregate struct or void");
    static_assert(std::is_void_v<Response> || Type<Response>::kind == TypeKind::Struct,
                  "Service response must be an aggregate struct or void");
    static_assert(Base::wrapsServiceResult
                      ? std::is_same_v<Result, WrappedResult>
                      : Base::borrowsResponse
                      ? std::is_same_v<Result, std::add_lvalue_reference_t<std::add_const_t<Response>>>
                      : std::is_same_v<Result, Response>,
                  "Service must return Response, void or the exact ServiceResult<Response>, BorrowedServiceResult<Response>, or const Response&");
};

} // namespace service_detail

template <class Binding>
class ServiceDefinition {
    using Shape = service_detail::Shape<typename Binding::Signature>;

public:
    using Request = typename Shape::Request;
    using Response = typename Shape::Response;
    static constexpr bool borrowsResponse = Shape::borrowsResponse;
    using Result = std::conditional_t<borrowsResponse,
        BorrowedServiceResult<Response>, ServiceResult<Response>>;

    constexpr ServiceDefinition(detail::Name name, Binding binding) noexcept
        : name_(name), binding_(binding)
    {} // detail::Name already validated the borrowed metadata.

    [[nodiscard]] constexpr const char* name() const noexcept { return name_; }

    [[nodiscard]] Result call() const noexcept
        requires std::is_void_v<Request>
    {
        return callResolved();
    }

    template <class Argument>
        requires (!std::is_void_v<Request> &&
                  std::is_same_v<std::remove_cvref_t<Argument>, Request> &&
                  !std::is_volatile_v<std::remove_reference_t<Argument>>)
    [[nodiscard]] Result call(Argument&& request) const noexcept
    {
        return callResolved(std::forward<Argument>(request));
    }

private:
    template <class... Definitions>
    friend class ServiceTable;

    using BindingType = Binding;

    template <class... Args>
    [[nodiscard]] Result callResolved(Args&&... args) const noexcept
    {
        // A slot returns one target snapshot; availability and invocation use
        // that same snapshot. Direct owners do not receive a null check.
        auto selected = binding_.snapshot();
        if (!Binding::available(selected))
            return Result::failure(ServiceStatus::Unavailable);

        return invokeSelected(selected, std::forward<Args>(args)...);
    }

    // Keep native and encoded instantiations separate. Otherwise GCC 13 -Os
    // shares this wrapper with the erased thunk and leaves an out-of-line
    // call even when the native owner/target are known. No forced inlining of
    // the callback or encoded boundary is needed; both paths have one body.
    template <bool Encoded = false, class Selected, class... Args>
    [[nodiscard]] static Result invokeSelected(Selected selected, Args&&... args) noexcept
    {
        if constexpr (Shape::wrapsServiceResult) {
            // Preserve application statuses for both owning and borrowed results.
            return Binding::invoke(selected, std::forward<Args>(args)...);
        } else if constexpr (borrowsResponse) {
            return Result::success(Binding::invoke(selected, std::forward<Args>(args)...));
        } else if constexpr (std::is_void_v<Response>) {
            Binding::invoke(selected, std::forward<Args>(args)...);
            return Result::success();
        } else {
            return Result::successFrom([&]() -> Response {
                return Binding::invoke(selected, std::forward<Args>(args)...);
            });
        }
    }

    const char* name_;
    Binding binding_;
};

// Template-target forms keep the exact function/method in the binding type.
template <auto Target>
    requires detail::functionPointer<decltype(Target)>
[[nodiscard]] constexpr auto service(detail::Name name) noexcept
{
    return ServiceDefinition{name, detail::StaticFunction<Target>{}};
}

template <auto Target, class Owner>
    requires std::is_member_function_pointer_v<decltype(Target)>
[[nodiscard]] constexpr auto service(detail::Name name, Owner& owner) noexcept
{
    return ServiceDefinition{name, detail::StaticMethod<Target, Owner>{
                                       std::addressof(owner)}};
}

template <auto Target, class Owner>
    requires (std::is_member_function_pointer_v<decltype(Target)> &&
              !std::is_lvalue_reference_v<Owner> &&
              !detail::ReferenceWrapper<std::remove_cvref_t<Owner>>::value)
void service(const char*, std::remove_reference_t<Owner>&&) = delete;

// Explicit Owner arguments must not let a proxy conversion hide the actual
// borrowed object. Deduce Argument separately, including its value category.
template <auto Target, class ExplicitOwner = void, class Argument>
    requires (std::is_member_function_pointer_v<decltype(Target)> &&
              !detail::ReferenceWrapper<std::remove_cvref_t<Argument>>::value &&
              !telemetry::detail::isBorrowedObjectArgument<ExplicitOwner, Argument>)
void service(const char*, Argument&&) = delete;

// A braced list otherwise bypasses Argument deduction. In particular, a
// temporary list element must never become the stored owner address.
template <auto Target, class Owner, class Argument>
    requires (std::is_member_function_pointer_v<decltype(Target)> &&
              !telemetry::detail::isBorrowedObjectArgument<Owner, Argument&>)
void service(const char*, std::initializer_list<Argument>) = delete;

template <auto Target, class Owner>
    requires std::is_member_function_pointer_v<decltype(Target)>
[[nodiscard]] constexpr auto service(detail::Name name,
                                     std::reference_wrapper<Owner> owner) noexcept
{
    return service<Target>(name, owner.get());
}

// A function pointer or stateless lambda is copied as an exact native pointer.
template <class Callable>
    requires detail::DirectFunction<Callable>
[[nodiscard]] constexpr auto service(detail::Name name, Callable callable) noexcept
{
    using Function = decltype(+callable);
    static_assert(noexcept(+callable),
                  "Service function-pointer conversion must be noexcept");
    return ServiceDefinition{name, detail::RuntimeFunction<Function>{+callable}};
}

// Stateful callables and every slot family are borrowed by stable address.
template <class Callable>
    requires (std::is_class_v<Callable> &&
              !detail::DirectFunction<Callable> &&
              !detail::ReferenceWrapper<std::remove_cv_t<Callable>>::value)
[[nodiscard]] constexpr auto service(detail::Name name, Callable& callable) noexcept
{
    return ServiceDefinition{name, detail::borrow(callable)};
}

template <class Callable>
    requires (std::is_class_v<std::remove_cvref_t<Callable>> &&
              !std::is_lvalue_reference_v<Callable> &&
              !detail::DirectFunction<std::remove_cvref_t<Callable>> &&
              !detail::ReferenceWrapper<std::remove_cvref_t<Callable>>::value)
void service(const char*, Callable&&) = delete;

template <class Callable>
[[nodiscard]] constexpr auto service(detail::Name name,
                                     std::reference_wrapper<Callable> callable) noexcept
{
    return ServiceDefinition{name, detail::borrow(callable.get())};
}

} // namespace telemetry

#endif
