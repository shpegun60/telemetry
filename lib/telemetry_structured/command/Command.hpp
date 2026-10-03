/*
 * @file Command.hpp
 * @brief Native commands with zero or one exact request structure.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_COMMAND_COMMAND_HPP
#define TELEMETRY_STRUCTURED_COMMAND_COMMAND_HPP

#include "../detail/Binding.hpp"
#include "../detail/Name.hpp"
#include <telemetry/command/TelemetryCommand.h>
#include <cstdlib>

namespace telemetry::structured {

template <class... Definitions> class CommandTable;

template <class Binding>
class CommandDefinition {
    using Shape = reflection::EndpointTraits<reflection::EndpointKind::Command,
                                              typename Binding::Signature>;
public:
    using Request = typename Shape::Request;
    static_assert(std::is_same_v<typename Shape::Result, telemetry::CommandResult>,
                  "Command must return telemetry::CommandResult");
    static_assert(std::is_void_v<Request> || Type<Request>::kind == TypeKind::Struct,
                  "Command request must be an aggregate struct or void");

    constexpr CommandDefinition(detail::Name name, Binding binding) noexcept
        : name_(name), binding_(binding)
    {} // detail::Name already validated the borrowed metadata.

    [[nodiscard]] constexpr const char* name() const noexcept { return name_; }

    [[nodiscard]] telemetry::CommandResult call() const noexcept
        requires std::is_void_v<Request>
    {
        return callResolved();
    }

    template <class Argument>
        requires (!std::is_void_v<Request> &&
                  std::is_same_v<std::remove_cvref_t<Argument>, Request> &&
                  !std::is_volatile_v<std::remove_reference_t<Argument>>)
    [[nodiscard]] telemetry::CommandResult call(Argument&& request) const noexcept
    {
        return callResolved(std::forward<Argument>(request));
    }

private:
    template <class... Definitions> friend class CommandTable;
    using BindingType = Binding;

    template <class... Args>
    [[nodiscard]] telemetry::CommandResult callResolved(Args&&... args) const noexcept
    {
        auto selected = binding_.snapshot();
        if (!Binding::available(selected)) return telemetry::CommandResult::Unavailable;
        return Binding::invoke(selected, std::forward<Args>(args)...);
    }

    const char* name_;
    Binding binding_;
};

template <auto Target>
    requires detail::functionPointer<decltype(Target)>
[[nodiscard]] constexpr auto command(detail::Name name) noexcept
{
    return CommandDefinition{name, detail::StaticFunction<Target>{}};
}

template <auto Target, class... Explicit, class Owner>
    requires (sizeof...(Explicit) == 0 &&
              std::is_member_function_pointer_v<decltype(Target)> &&
              detail::StableOwner<Owner>)
[[nodiscard]] constexpr auto command(detail::Name name, Owner&& owner) noexcept
{
    return CommandDefinition{name, detail::method<Target>(std::forward<Owner>(owner))};
}

template <class... Explicit, class Callable>
    requires (sizeof...(Explicit) == 0 && detail::Bindable<Callable>)
[[nodiscard]] constexpr auto command(detail::Name name, Callable&& callable) noexcept
{
    return CommandDefinition{name, detail::binding(std::forward<Callable>(callable))};
}

template <class First, class Second, class... Rest>
constexpr void command(const char*, First&&, Second&&, Rest&&...) noexcept
{
    static_assert(!std::is_same_v<First, First>,
                  "Structured command accepts name and one binding only; semantic metadata is not supported");
}

} // namespace telemetry::structured

#endif
