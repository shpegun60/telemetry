/*
 * @file Binding.hpp
 * @brief Shared native target storage, lifetime rules and single-snapshot invocation.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_DETAIL_BINDING_HPP
#define TELEMETRY_STRUCTURED_DETAIL_BINDING_HPP

#include "../reflection/Callable.hpp"
#include <telemetry/detail/TelemetryOwner.h>
#include <telemetry/detail/TelemetryTarget.h>
#include <telemetry/slot/TelemetryContextFunctionSlot.h>
#include <telemetry/slot/TelemetryDelegateRefSlot.h>
#include <telemetry/slot/TelemetryDelegateSlot.h>
#include <telemetry/slot/TelemetryFunctionSlot.h>
#include <telemetry/slot/TelemetryOwnerSlot.h>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry::structured::detail {

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

// Runtime entries store the object actually needed by a binding. Known
// methods/callables/slots need no extra pointer through their definition.
// Runtime function pointers stay in a Binding object: a function pointer is
// never converted to void*, and custom bindings retain their snapshot hook.
template <class Binding>
struct ErasedBinding {
    [[nodiscard]] static constexpr const void* context(const Binding& binding) noexcept
    { return std::addressof(binding); }
    [[nodiscard]] static auto snapshot(const void* context) noexcept
    { return static_cast<const Binding*>(context)->snapshot(); }
};

template <class Object>
[[nodiscard]] constexpr const void* eraseBindingObject(Object* object) noexcept
{
    return const_cast<const std::remove_volatile_t<Object>*>(object);
}

template <class Object>
[[nodiscard]] Object* restoreBindingObject(const void* context) noexcept
{
    // Restore the original cv-qualified type, not a writable view of const data.
    return static_cast<Object*>(const_cast<void*>(context));
}

template <auto Target>
struct ErasedBinding<StaticFunction<Target>> {
    [[nodiscard]] static constexpr const void* context(const StaticFunction<Target>&) noexcept
    { return nullptr; }
    [[nodiscard]] static constexpr bool snapshot(const void*) noexcept { return true; }
};

template <auto Target, class Owner>
struct ErasedBinding<StaticMethod<Target, Owner>> {
    [[nodiscard]] static constexpr const void* context(const StaticMethod<Target, Owner>& binding) noexcept
    { return eraseBindingObject(binding.owner); }
    [[nodiscard]] static auto snapshot(const void* context) noexcept
    {
        auto* owner = restoreBindingObject<Owner>(context);
        if constexpr (telemetry::detail::isOwnerSlot<Owner>) return owner->get();
        else return owner;
    }
};

template <class Callable>
struct ErasedBinding<BorrowedCallable<Callable>> {
    [[nodiscard]] static constexpr const void* context(const BorrowedCallable<Callable>& binding) noexcept
    { return eraseBindingObject(binding.callable); }
    [[nodiscard]] static Callable* snapshot(const void* context) noexcept
    { return restoreBindingObject<Callable>(context); }
};

template <class Slot>
struct ErasedBinding<BorrowedSlot<Slot>> {
    [[nodiscard]] static constexpr const void* context(const BorrowedSlot<Slot>& binding) noexcept
    { return eraseBindingObject(binding.slot); }
    [[nodiscard]] static auto snapshot(const void* context) noexcept
    { return restoreBindingObject<Slot>(context)->get(); }
};


// Field and Command reuse the proven target storage/snapshot/invocation
// classes. Service semantics and its public factories remain unchanged.
template <class T>
inline constexpr bool callable = DirectFunction<std::decay_t<T>> ||
    telemetry::detail::isCallableSlot<std::remove_cvref_t<T>> ||
    ReferenceWrapper<std::remove_cvref_t<T>>::value ||
    requires { &std::remove_cvref_t<T>::operator(); };

template <class T>
concept Bindable = callable<T> &&
    (DirectFunction<std::decay_t<T>> || std::is_lvalue_reference_v<T> ||
     ReferenceWrapper<std::remove_cvref_t<T>>::value);

template <class Callable>
    requires Bindable<Callable>
[[nodiscard]] constexpr auto binding(Callable&& value) noexcept
{
    if constexpr (DirectFunction<std::decay_t<Callable>>) {
        using Function = decltype(+value);
        static_assert(noexcept(+value), "Function-pointer conversion must be noexcept");
        return RuntimeFunction<Function>{+value};
    } else if constexpr (ReferenceWrapper<std::remove_cvref_t<Callable>>::value) {
        return borrow(value.get());
    } else {
        return borrow(value);
    }
}

template <class Owner>
concept StableOwner = std::is_lvalue_reference_v<Owner> ||
    ReferenceWrapper<std::remove_cvref_t<Owner>>::value;

template <auto Target, class Owner>
    requires StableOwner<Owner>
[[nodiscard]] constexpr auto method(Owner&& owner) noexcept
{
    if constexpr (ReferenceWrapper<std::remove_cvref_t<Owner>>::value) {
        return method<Target>(owner.get());
    } else {
        using Object = std::remove_reference_t<Owner>;
        return StaticMethod<Target, Object>{std::addressof(owner)};
    }
}

} // namespace telemetry::structured::detail

#endif
