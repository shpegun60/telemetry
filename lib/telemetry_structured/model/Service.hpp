/*
 * @file Service.hpp
 * @brief Native service bindings with inferred request and response types.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_MODEL_SERVICE_HPP
#define TELEMETRY_STRUCTURED_MODEL_SERVICE_HPP

#include "../reflection/Callable.hpp"
#include "../result/ServiceResult.hpp"
#include "../type/Traits.hpp"

#include <telemetry/detail/TelemetryOwner.h>
#include <telemetry/detail/TelemetryTarget.h>
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

namespace telemetry::structured {
template <class... Definitions>
class ServiceTable;

namespace service_detail {

template <class Signature>
struct Shape : reflection::EndpointTraits<reflection::EndpointKind::Service, Signature> {
    using Base = reflection::EndpointTraits<reflection::EndpointKind::Service, Signature>;
    using Request = typename Base::Request;
    using Response = typename Base::Response;
    using Result = typename Base::Result;

    static_assert(std::is_void_v<Request> || Type<Request>::kind == TypeKind::Struct,
                  "Service request must be an aggregate struct or void");
    static_assert(std::is_void_v<Response> || Type<Response>::kind == TypeKind::Struct,
                  "Service response must be an aggregate struct or void");
    static_assert(Base::wrapsServiceResult
                      ? std::is_same_v<Result, ServiceResult<Response>>
                      : std::is_same_v<Result, Response>,
                  "Service must return Response, void or the exact ServiceResult<Response>");
};

template <class T>
inline constexpr bool functionPointer = std::is_pointer_v<T> &&
    std::is_function_v<std::remove_pointer_t<T>>;

template <class T>
concept DirectFunction = requires(T value) {
    requires functionPointer<std::remove_cvref_t<T>> ||
             (std::is_class_v<std::remove_cvref_t<T>> &&
              std::is_empty_v<std::remove_cvref_t<T>>);
    +value;
    requires functionPointer<decltype(+value)>;
    requires std::is_convertible_v<T, decltype(+value)>;
};

template <class T>
struct ReferenceWrapper : std::false_type {};

template <class T>
struct ReferenceWrapper<std::reference_wrapper<T>> : std::true_type {};

template <class Owner, bool Slot = telemetry::detail::isOwnerSlot<Owner>>
struct OwnerObject {
    using type = Owner;
};

template <class Owner>
struct OwnerObject<Owner, true> {
    using type = typename Owner::Owner;
};

template <class Function, class Object, class Arguments>
struct MemberInvocable;

template <class Function, class Object, class... Args>
struct MemberInvocable<Function, Object, std::tuple<Args...>>
    : std::bool_constant<std::is_nothrow_invocable_v<Function, Object&, Args...>> {};

template <class Function, class Arguments>
struct CallableInvocable;

template <class Function, class... Args>
struct CallableInvocable<Function, std::tuple<Args...>>
    : std::bool_constant<std::is_nothrow_invocable_v<Function&, Args...>> {};

template <auto Target>
struct StaticFunction {
    using Signature = decltype(Target);
    static_assert(telemetry::detail::nonNullTarget<Target>,
                  "Service target cannot be nullptr");
    static_assert(functionPointer<Signature>, "Service target must be a function");

    [[nodiscard]] constexpr bool snapshot() const noexcept { return true; }
    [[nodiscard]] static bool available(bool) noexcept
    {
        return telemetry::detail::targetAvailable<Target>();
    }
    template <class... Args>
    static decltype(auto) invoke(bool, Args&&... args) noexcept
    {
        return Target(std::forward<Args>(args)...);
    }
};

template <auto Target, class Owner>
struct StaticMethod {
    using Signature = decltype(Target);
    using Object = typename OwnerObject<Owner>::type;
    static_assert(telemetry::detail::nonNullTarget<Target>,
                  "Service target cannot be nullptr");
    static_assert(std::is_member_function_pointer_v<Signature>,
                  "Service target must be a method");
    static_assert(telemetry::detail::isDirectMemberOwner<Signature, Object>,
                  "Service method requires a direct owner or OwnerSlot of that owner");
    static_assert(MemberInvocable<Signature, Object,
                  typename reflection::Function<Signature>::Arguments>::value,
                  "Service method must be noexcept-invocable on this owner");

    Owner* owner;

    [[nodiscard]] constexpr auto snapshot() const noexcept
    {
        if constexpr (telemetry::detail::isOwnerSlot<Owner>) return owner->get();
        else return owner;
    }
    template <class Pointer>
    [[nodiscard]] static bool available(Pointer selected) noexcept
    {
        if constexpr (telemetry::detail::isOwnerSlot<Owner>) {
            if (selected == nullptr) return false;
        }
        return telemetry::detail::targetAvailable<Target>();
    }
    template <class Pointer, class... Args>
    static decltype(auto) invoke(Pointer selected, Args&&... args) noexcept
    {
        // Convert a derived owner to the declaring base subobject first.
        // GCC otherwise warns about a member-pointer access through the
        // derived pointer under strict aliasing. For an exact owner this cast
        // is a no-op, preserving the direct-call codegen at -Os.
        using Declaring = typename reflection::Function<Signature>::Owner;
        using MaybeConst = std::conditional_t<std::is_const_v<Object>,
                                              const Declaring, Declaring>;
        using Base = std::conditional_t<std::is_volatile_v<Object>,
                                        volatile MaybeConst, MaybeConst>;
        auto* base = static_cast<Base*>(selected);
        return (base->*Target)(std::forward<Args>(args)...);
    }
};

template <class Function>
struct RuntimeFunction {
    using Signature = Function;
    static_assert(functionPointer<Function>, "Service callback must be a function pointer");

    Function function;

    [[nodiscard]] constexpr Function snapshot() const noexcept { return function; }
    [[nodiscard]] static bool available(Function selected) noexcept
    {
        return telemetry::detail::pointerPresent(selected);
    }
    template <class... Args>
    static decltype(auto) invoke(Function selected, Args&&... args) noexcept
    {
        return selected(std::forward<Args>(args)...);
    }
};

template <class Callable>
struct BorrowedCallable {
    using Signature = std::remove_cv_t<Callable>;
    static_assert(CallableInvocable<Callable,
                  typename reflection::Function<Signature>::Arguments>::value,
                  "Service callable must be noexcept-invocable as borrowed");
    Callable* callable;

    [[nodiscard]] constexpr Callable* snapshot() const noexcept { return callable; }
    [[nodiscard]] static constexpr bool available(Callable*) noexcept { return true; }
    template <class... Args>
    static decltype(auto) invoke(Callable* selected, Args&&... args) noexcept
    {
        return (*selected)(std::forward<Args>(args)...);
    }
};

template <class Slot>
struct BorrowedSlot {
    using Signature = typename std::remove_cv_t<Slot>::Signature;
    Slot* slot;

    [[nodiscard]] constexpr auto snapshot() const noexcept { return slot->get(); }
    template <class Target>
    [[nodiscard]] static bool available(const Target& selected) noexcept
    {
        return static_cast<bool>(selected);
    }
    template <class Target, class... Args>
    static decltype(auto) invoke(Target selected, Args&&... args) noexcept
    {
        if constexpr (std::is_pointer_v<Target>)
            return selected(std::forward<Args>(args)...);
        else
            return selected.invoke(std::forward<Args>(args)...);
    }
};

template <class Callable>
[[nodiscard]] constexpr auto borrow(Callable& callable) noexcept
{
    if constexpr (telemetry::detail::isCallableSlot<Callable>)
        return BorrowedSlot<Callable>{std::addressof(callable)};
    else
        return BorrowedCallable<Callable>{std::addressof(callable)};
}

} // namespace service_detail

template <class Binding>
class ServiceDefinition {
    using Shape = service_detail::Shape<typename Binding::Signature>;

public:
    using Request = typename Shape::Request;
    using Response = typename Shape::Response;
    using Result = ServiceResult<Response>;

    constexpr ServiceDefinition(const char* name, Binding binding) noexcept
        : name_(name), binding_(binding)
    {
        if (name == nullptr || name[0] == '\0') std::abort();
    }

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

    template <class Selected, class... Args>
    [[nodiscard]] static Result invokeSelected(Selected selected, Args&&... args) noexcept
    {
        if constexpr (Shape::wrapsServiceResult) {
            return Binding::invoke(selected, std::forward<Args>(args)...);
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
    requires service_detail::functionPointer<decltype(Target)>
[[nodiscard]] constexpr auto service(const char* name) noexcept
{
    return ServiceDefinition{name, service_detail::StaticFunction<Target>{}};
}

template <auto Target, class Owner>
    requires std::is_member_function_pointer_v<decltype(Target)>
[[nodiscard]] constexpr auto service(const char* name, Owner& owner) noexcept
{
    return ServiceDefinition{name, service_detail::StaticMethod<Target, Owner>{
                                       std::addressof(owner)}};
}

template <auto Target, class Owner>
    requires (std::is_member_function_pointer_v<decltype(Target)> &&
              !std::is_lvalue_reference_v<Owner> &&
              !service_detail::ReferenceWrapper<std::remove_cvref_t<Owner>>::value)
void service(const char*, std::remove_reference_t<Owner>&&) = delete;

// Explicit Owner arguments must not let a proxy conversion hide the actual
// borrowed object. Deduce Argument separately, including its value category.
template <auto Target, class ExplicitOwner = void, class Argument>
    requires (std::is_member_function_pointer_v<decltype(Target)> &&
              !service_detail::ReferenceWrapper<std::remove_cvref_t<Argument>>::value &&
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
[[nodiscard]] constexpr auto service(const char* name,
                                     std::reference_wrapper<Owner> owner) noexcept
{
    return service<Target>(name, owner.get());
}

// A function pointer or stateless lambda is copied as an exact native pointer.
template <class Callable>
    requires service_detail::DirectFunction<Callable>
[[nodiscard]] constexpr auto service(const char* name, Callable callable) noexcept
{
    using Function = decltype(+callable);
    static_assert(noexcept(+callable),
                  "Service function-pointer conversion must be noexcept");
    return ServiceDefinition{name, service_detail::RuntimeFunction<Function>{+callable}};
}

// Stateful callables and every slot family are borrowed by stable address.
template <class Callable>
    requires (std::is_class_v<Callable> &&
              !service_detail::DirectFunction<Callable> &&
              !service_detail::ReferenceWrapper<std::remove_cv_t<Callable>>::value)
[[nodiscard]] constexpr auto service(const char* name, Callable& callable) noexcept
{
    return ServiceDefinition{name, service_detail::borrow(callable)};
}

template <class Callable>
    requires (std::is_class_v<std::remove_cvref_t<Callable>> &&
              !std::is_lvalue_reference_v<Callable> &&
              !service_detail::DirectFunction<std::remove_cvref_t<Callable>> &&
              !service_detail::ReferenceWrapper<std::remove_cvref_t<Callable>>::value)
void service(const char*, Callable&&) = delete;

template <class Callable>
[[nodiscard]] constexpr auto service(const char* name,
                                     std::reference_wrapper<Callable> callable) noexcept
{
    return ServiceDefinition{name, service_detail::borrow(callable.get())};
}

} // namespace telemetry::structured

#endif
