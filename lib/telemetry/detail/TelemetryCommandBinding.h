/**
 * @file TelemetryCommandBinding.h
 * @brief Compile-time command signatures, adapters and parameter descriptions.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_BINDING_H
#define TELEMETRY_COMMAND_BINDING_H

#include "../command/TelemetryCommand.h"
#include "../command/TelemetryCommandArgs.h"
#include <memory>

namespace telemetry {
namespace detail {

template <class Metadata, std::size_t Position, bool Present>
struct CommandMetadataConstraint {
    using Type = NoLimits;
};

template <class Metadata, std::size_t Position>
struct CommandMetadataConstraint<Metadata, Position, true> {
    using Entry = std::tuple_element_t<Position,
        std::decay_t<decltype(std::declval<Metadata>().entries)>>;
    using Type = std::decay_t<decltype(std::declval<Entry>().values)>;
};

template <class Traits, class Metadata>
struct CommandContract {
    using Arguments = typename Traits::Arguments;

    template <std::size_t I>
    using MetadataSlot = CommandMetadataSlot<Metadata, I>;

    template <std::size_t I>
    static constexpr bool hasMetadata = MetadataSlot<I>::present;

    template <std::size_t I>
    static constexpr std::size_t metadataPosition = MetadataSlot<I>::position;

    template <std::size_t I>
    using Constraint = typename CommandMetadataConstraint<
        Metadata, metadataPosition<I>, hasMetadata<I>>::Type;

    template <std::size_t I>
    static constexpr bool hasBoundedMetadata = IsBoundedLimits<Constraint<I>>::value;

    template <std::size_t I>
    static constexpr CommandParam parameter(const Metadata* metadata) noexcept
    {
        using T = std::tuple_element_t<I, Arguments>;
        if constexpr (!hasMetadata<I>) {
            return {I, nullptr, nullptr, inferredType<T>()};
        } else {
            const auto& entry = std::get<metadataPosition<I>>(metadata->entries);
            if (entry.name == nullptr || entry.unit == nullptr) invalidFieldLimits();
            return {I, entry.name, entry.unit, refineType<T>(entry.values)};
        }
    }

    template <std::size_t I>
    TELEMETRY_FORCE_INLINE static bool validateNative(
        const Metadata* metadata,
        RawNumberT<std::tuple_element_t<I, Arguments>> number) noexcept
    {
        using T = std::tuple_element_t<I, Arguments>;
        using Raw = RawNumberT<T>;
        if constexpr (std::is_floating_point_v<Raw> && !hasBoundedMetadata<I>) {
            if (!scalarFinite(number)) return false;
        }
        if constexpr (std::is_enum_v<T>) {
            constexpr auto bounds = enumConstraintBounds<T, Constraint<I>>();
            if (number < bounds.minimum || number > bounds.maximum) return false;
        }
        if constexpr (hasMetadata<I>) {
            if (!within(number,
                        std::get<metadataPosition<I>>(metadata->entries).values)) return false;
        }
        return true;
    }

    template <std::size_t I>
    static bool convert(const Metadata* metadata, const Scalar& input,
                        Arguments& output) noexcept
    {
        using T = std::tuple_element_t<I, Arguments>;
        using Raw = RawNumberT<T>;
        const auto number = convertScalar<Raw>(input);
        if (!number || !validateNative<I>(metadata, *number)) return false;
        std::get<I>(output) = static_cast<T>(*number);
        return true;
    }

    template <std::size_t... I>
    static bool convertAll(const Metadata* metadata, const Scalar* values,
                           Arguments& output, std::index_sequence<I...>) noexcept
    {
        (void) metadata;
        (void) values;
        return (convert<I>(metadata, values[I], output) && ...);
    }

    template <std::size_t... I, class... Input>
    TELEMETRY_FORCE_INLINE static bool validateNativeAll(
        const Metadata* metadata, std::index_sequence<I...>, Input... values) noexcept
    {
        static_assert(sizeof...(I) == sizeof...(Input),
                      "Typed command argument count must match the target signature");
        static_assert(std::is_same_v<std::tuple<std::decay_t<Input>...>, Arguments>,
                      "Typed command values must exactly match the target signature");
        auto supplied = std::forward_as_tuple(values...);
        (void) metadata;
        (void) supplied;
        return (validateNative<I>(metadata,
                    static_cast<RawNumberT<std::tuple_element_t<I, Arguments>>>(
                        std::get<I>(supplied))) && ...);
    }

    template <class T>
    TELEMETRY_FORCE_INLINE static bool within(T, NoLimits) noexcept { return true; }
    template <class T, class U, bool Bounded>
    TELEMETRY_FORCE_INLINE static bool within(
        T number, const ValueLimits<U, Bounded>& values) noexcept
    {
        if constexpr (Bounded) return number >= static_cast<T>(values.minimum)
                                   && number <= static_cast<T>(values.maximum);
        else return true;
    }
    template <class T, class E, E... Values>
    TELEMETRY_FORCE_INLINE static bool within(
        T, const EnumSpec<E, Values...>&) noexcept { return true; }

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

    static constexpr void validateMetadata(const Metadata* metadata) noexcept
    {
        if constexpr (std::is_same_v<Metadata, NoCommandArgs>) {
            validate(metadata, std::make_index_sequence<Traits::arity>{});
        } else if constexpr (Metadata::positional) {
            static_assert(Metadata::count == Traits::arity,
                          "Positional command metadata count must match the function's parameter count");
            if constexpr (Metadata::count == Traits::arity)
                validate(metadata, std::make_index_sequence<Traits::arity>{});
        } else if constexpr (Metadata::indexed) {
            static_assert(Metadata::indicesUnique,
                          "Indexed command metadata positions must be unique");
            static_assert(Metadata::template indicesInRange<Traits::arity>,
                          "Indexed command metadata position is outside the function's parameter list");
            if constexpr (Metadata::indicesUnique
                          && Metadata::template indicesInRange<Traits::arity>)
                validate(metadata, std::make_index_sequence<Traits::arity>{});
        } else {
            static_assert(Metadata::positional || Metadata::indexed,
                          "Command metadata cannot mix positional arg(...) and indexed arg<N>(...)");
        }
    }
};

template <auto Target, class Owner, class Metadata = NoCommandArgs>
struct CommandBinding {
    using Traits = CallableTraits<decltype(Target)>;
    using Contract = CommandContract<Traits, Metadata>;
    using Arguments = typename Contract::Arguments;
    static_assert(Target != nullptr, "Command target cannot be null");
    static_assert(std::is_same_v<typename Traits::Result, CommandResult>,
                  "Command target must return CommandResult");

    template <std::size_t... I>
    static CommandResult run(const void* target, const void* metadata, const Scalar* values,
                             std::index_sequence<I...> sequence) noexcept
    {
        Arguments converted{};
        const auto* definition = static_cast<const Metadata*>(metadata);
        if (!Contract::convertAll(definition, values, converted, sequence))
            return CommandResult::InvalidValue;
        // The factory admitted an lvalue with this exact cv-qualified type.
        auto* owner = static_cast<Owner*>(const_cast<void*>(target));
        return invokeFactory<Target>(owner, std::get<I>(converted)...);
    }

    static CommandResult run(const void* target, const void* metadata,
                             const Scalar* values, std::size_t count) noexcept
    {
        if (count != Traits::arity) return CommandResult::ArgumentCountMismatch;
        if constexpr (Traits::arity != 0) {
            if (values == nullptr) return CommandResult::InvalidValue;
        }
        return run(target, metadata, values, std::make_index_sequence<Traits::arity>{});
    }

    template <std::size_t... I, class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult callNative(
        const void* target, const Metadata* metadata,
        std::index_sequence<I...> sequence, Input... values) noexcept
    {
        if (!Contract::validateNativeAll(metadata, sequence, values...))
            return CommandResult::InvalidValue;
        auto* owner = static_cast<Owner*>(const_cast<void*>(target));
        return invokeFactory<Target>(owner, values...);
    }

    template <class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult callNative(
        const void* target, const Metadata* metadata, Input... values) noexcept
    {
        static_assert(sizeof...(Input) == Traits::arity,
                      "Typed command argument count must match the target signature");
        return callNative(target, metadata,
                          std::make_index_sequence<Traits::arity>{}, values...);
    }

    static constexpr Command make(const char* name, Owner* owner,
                                  const Metadata* metadata = nullptr) noexcept
    {
        Contract::validateMetadata(metadata);
        return Command{name, owner, metadata, &run, &Contract::schema};
    }
};

template <class Callable, class Metadata = NoCommandArgs>
struct BorrowedCommandBinding {
    static_assert(HasConcreteCallOperator<Callable>::value,
                  "Borrowed command callable must have one concrete operator(); generic and overloaded callables are unsupported");
    using Traits = CallableObjectTraits<Callable>;
    using Contract = CommandContract<Traits, Metadata>;
    using Arguments = typename Contract::Arguments;
    static_assert(std::is_same_v<typename Traits::Result, CommandResult>,
                  "Borrowed command callable must return CommandResult");

    template <std::size_t... I>
    static CommandResult run(const void* target, const void* metadata, const Scalar* values,
                             std::index_sequence<I...> sequence) noexcept
    {
        Arguments converted{};
        const auto* definition = static_cast<const Metadata*>(metadata);
        if (!Contract::convertAll(definition, values, converted, sequence))
            return CommandResult::InvalidValue;
        auto* callable = static_cast<Callable*>(const_cast<void*>(target));
        return (*callable)(std::get<I>(converted)...);
    }

    static CommandResult run(const void* target, const void* metadata,
                             const Scalar* values, std::size_t count) noexcept
    {
        if (count != Traits::arity) return CommandResult::ArgumentCountMismatch;
        if constexpr (Traits::arity != 0) {
            if (values == nullptr) return CommandResult::InvalidValue;
        }
        return run(target, metadata, values, std::make_index_sequence<Traits::arity>{});
    }

    template <std::size_t... I, class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult callNative(
        const void* target, const Metadata* metadata,
        std::index_sequence<I...> sequence, Input... values) noexcept
    {
        if (!Contract::validateNativeAll(metadata, sequence, values...))
            return CommandResult::InvalidValue;
        auto* callable = static_cast<Callable*>(const_cast<void*>(target));
        return (*callable)(values...);
    }

    template <class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult callNative(
        const void* target, const Metadata* metadata, Input... values) noexcept
    {
        static_assert(sizeof...(Input) == Traits::arity,
                      "Typed command argument count must match the target signature");
        return callNative(target, metadata,
                          std::make_index_sequence<Traits::arity>{}, values...);
    }

    static constexpr Command make(const char* name, Callable* callable,
                                  const Metadata* metadata = nullptr) noexcept
    {
        Contract::validateMetadata(metadata);
        return Command{name, callable, metadata, &run, &Contract::schema};
    }
};

// Member functions borrow only lvalues; free/static functions need no owner.
template <auto Target, class Owner, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)> && std::is_lvalue_reference_v<Owner&&>, int> = 0>
constexpr Command materializeCommand(const char* name, Owner&& owner) noexcept
{
    return detail::CommandBinding<Target, std::remove_reference_t<Owner>>::make(name, std::addressof(owner));
}
template <auto Target, std::enable_if_t<!std::is_member_function_pointer_v<decltype(Target)>, int> = 0>
constexpr Command materializeCommand(const char* name) noexcept
{
    return detail::CommandBinding<Target, detail::NoOwner>::make(name, nullptr);
}
template <auto Target, class Owner, class Metadata, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)> && std::is_lvalue_reference_v<Owner&&>
          && std::is_lvalue_reference_v<Metadata&&> && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command materializeCommand(const char* name, Owner&& owner, Metadata&& metadata) noexcept
{
    return detail::CommandBinding<Target, std::remove_reference_t<Owner>, std::decay_t<Metadata>>::make(
        name, std::addressof(owner), std::addressof(metadata));
}
template <auto Target, class Metadata, std::enable_if_t<
          !std::is_member_function_pointer_v<decltype(Target)> && std::is_lvalue_reference_v<Metadata&&>
          && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command materializeCommand(const char* name, Metadata&& metadata) noexcept
{
    return detail::CommandBinding<Target, detail::NoOwner, std::decay_t<Metadata>>::make(
        name, nullptr, std::addressof(metadata));
}


// Stateful functors and named lambdas are borrowed as stable lvalues. Free and
// static functions deliberately use materializeCommand<&function>(), which stores no
// runtime target. The callable and optional metadata must outlive Command.
template <class Callable, std::enable_if_t<
          std::is_class_v<std::remove_cv_t<Callable>>, int> = 0>
constexpr Command materializeCommand(const char* name, Callable& callable) noexcept
{
    return detail::BorrowedCommandBinding<Callable>::make(
        name, std::addressof(callable));
}

template <class Callable, class Metadata, std::enable_if_t<
          std::is_class_v<std::remove_cv_t<Callable>>
          && std::is_lvalue_reference_v<Metadata&&>
          && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command materializeCommand(const char* name, Callable& callable,
                              Metadata&& metadata) noexcept
{
    return detail::BorrowedCommandBinding<Callable, std::decay_t<Metadata>>::make(
        name, std::addressof(callable), std::addressof(metadata));
}

template <class Callable, std::enable_if_t<
          std::is_class_v<std::remove_cv_t<Callable>>
          && !std::is_lvalue_reference_v<Callable>, int> = 0>
Command materializeCommand(const char*, Callable&&) = delete;

template <class Callable, class Metadata, std::enable_if_t<
          std::is_class_v<std::remove_cv_t<Callable>>
          && !std::is_lvalue_reference_v<Callable>
          && detail::isCommandArgs<Metadata>, int> = 0>
Command materializeCommand(const char*, Callable&&, Metadata&&) = delete;
} // namespace detail
} // namespace telemetry
#endif
