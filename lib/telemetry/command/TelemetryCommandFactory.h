/**
 * @file TelemetryCommandFactory.h
 * @brief Compile-time command signatures, adapters and parameter descriptions.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_FACTORY_H
#define TELEMETRY_COMMAND_FACTORY_H

#include "TelemetryCommand.h"
#include "TelemetryCommandArgs.h"
#include <memory>

namespace telemetry {
namespace detail {

template <auto Target, class Owner, class Metadata = NoCommandArgs>
struct CommandBinding {
    using Traits = CallableTraits<decltype(Target)>;
    using Arguments = typename Traits::Arguments;
    static_assert(Target != nullptr, "Command target cannot be null");
    static_assert(std::is_same_v<typename Traits::Result, CommandResult>,
                  "Command target must return CommandResult");

    template <std::size_t I>
    static constexpr CommandParam parameter(const Metadata* metadata) noexcept
    {
        using T = std::tuple_element_t<I, Arguments>;
        if constexpr (std::is_same_v<Metadata, NoCommandArgs>) {
            return {I, nullptr, nullptr, inferredType<T>()};
        } else {
            const auto& entry = std::get<I>(metadata->entries);
            if (entry.name == nullptr || entry.unit == nullptr) invalidFieldLimits();
            return {I, entry.name, entry.unit, refineType<T>(entry.values)};
        }
    }

    template <std::size_t I>
    static bool convert(const Metadata* metadata, const Scalar& input, Arguments& output) noexcept
    {
        using T = std::tuple_element_t<I, Arguments>;
        using Raw = RawNumberT<T>;
        const auto number = convertScalar<Raw>(input);
        if (!number) return false;
        if constexpr (std::is_floating_point_v<Raw>) {
            if (!scalarFinite(*number)) return false;
        }
        if constexpr (std::is_enum_v<T>) {
            constexpr auto type = inferredType<T>();
            using Stored = Scalar::NativeType<Scalar::from(Raw{}).type()>;
            if (*number < type.minimum().template get<Stored>()
                || *number > type.maximum().template get<Stored>()) return false;
        }
        if constexpr (!std::is_same_v<Metadata, NoCommandArgs>) {
            if (!within(*number, std::get<I>(metadata->entries).values)) return false;
        }
        std::get<I>(output) = static_cast<T>(*number);
        return true;
    }

    template <class T> static bool within(T, NoLimits) noexcept { return true; }
    template <class T, class U, bool Bounded>
    static bool within(T number, const ValueLimits<U, Bounded>& values) noexcept
    {
        if constexpr (Bounded) return number >= static_cast<T>(values.minimum)
                                   && number <= static_cast<T>(values.maximum);
        else return true;
    }

    template <std::size_t... I>
    static CommandResult run(const void* object, const void* metadata, const Scalar* values,
                             std::index_sequence<I...>) noexcept
    {
        Arguments converted{};
        const auto* definition = static_cast<const Metadata*>(metadata);
        (void) definition;
        (void) values;
        if (!(convert<I>(definition, values[I], converted) && ...)) return CommandResult::InvalidValue;
        // The factory admitted an lvalue with this exact cv-qualified type.
        auto* owner = static_cast<Owner*>(const_cast<void*>(object));
        return invokeFactory<Target>(owner, std::get<I>(converted)...);
    }

    static CommandResult run(const void* object, const void* metadata,
                             const Scalar* values, std::size_t count) noexcept
    {
        if (count != Traits::arity) return CommandResult::ArgumentCountMismatch;
        if constexpr (Traits::arity != 0) {
            if (values == nullptr) return CommandResult::InvalidValue;
        }
        return run(object, metadata, values, std::make_index_sequence<Traits::arity>{});
    }

    template <std::size_t... I>
    static bool schema(const Metadata* metadata, void* context, CommandParamSink sink,
                        std::index_sequence<I...>) noexcept
    {
        (void) metadata;
        (void) context;
        (void) sink;
        return (sink(context, parameter<I>(metadata)) && ...);
    }
    static bool schema(const void* metadata, void* context, CommandParamSink sink) noexcept
    {
        return schema(static_cast<const Metadata*>(metadata), context, sink,
                      std::make_index_sequence<Traits::arity>{});
    }

    template <std::size_t... I>
    static constexpr void validate(const Metadata* metadata, std::index_sequence<I...>) noexcept
    {
        (void) metadata;
        static_assert((isFactoryValue<std::tuple_element_t<I, Arguments>> && ...),
                      "Command parameters must be numeric or enum values, without references");
        (static_cast<void>(parameter<I>(metadata)), ...);
    }

    static constexpr Command make(CommandId id, const char* name, Owner* owner,
                                  const Metadata* metadata = nullptr) noexcept
    {
        if constexpr (!std::is_same_v<Metadata, NoCommandArgs>) {
            static_assert(Metadata::count == Traits::arity,
                          "Command metadata count must match the function's parameter count");
        }
        validate(metadata, std::make_index_sequence<Traits::arity>{});
        return Command{id, name, owner, metadata, &run, &schema};
    }
};
} // namespace detail

// Member functions borrow only lvalues; free/static functions need no owner.
template <auto Target, class Owner, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)> && std::is_lvalue_reference_v<Owner&&>, int> = 0>
constexpr Command makeCommand(CommandId id, const char* name, Owner&& owner) noexcept
{
    return detail::CommandBinding<Target, std::remove_reference_t<Owner>>::make(id, name, std::addressof(owner));
}
template <auto Target, std::enable_if_t<!std::is_member_function_pointer_v<decltype(Target)>, int> = 0>
constexpr Command makeCommand(CommandId id, const char* name) noexcept
{
    return detail::CommandBinding<Target, detail::NoOwner>::make(id, name, nullptr);
}
template <auto Target, class Owner, class Metadata, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)> && std::is_lvalue_reference_v<Owner&&>
          && std::is_lvalue_reference_v<Metadata&&> && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command makeCommand(CommandId id, const char* name, Owner&& owner, Metadata&& metadata) noexcept
{
    return detail::CommandBinding<Target, std::remove_reference_t<Owner>, std::decay_t<Metadata>>::make(
        id, name, std::addressof(owner), std::addressof(metadata));
}
template <auto Target, class Metadata, std::enable_if_t<
          !std::is_member_function_pointer_v<decltype(Target)> && std::is_lvalue_reference_v<Metadata&&>
          && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command makeCommand(CommandId id, const char* name, Metadata&& metadata) noexcept
{
    return detail::CommandBinding<Target, detail::NoOwner, std::decay_t<Metadata>>::make(
        id, name, nullptr, std::addressof(metadata));
}
} // namespace telemetry
#endif
