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
#include "TelemetryTarget.h"
#include <memory>

namespace telemetry {
namespace detail {

template <class Metadata, std::size_t Position, bool Present>
struct CommandMetadataConstraint {
    // Missing metadata must not form tuple_element<sentinel, ...>.
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
    // One contract serves native calls, Scalar conversion and schema output.
    // Thus a typed shortcut cannot silently bypass enum or numeric limits.
    using Arguments = typename Traits::Arguments;
    template <class... Input>
    static constexpr bool exactArguments = std::is_same_v<
        std::tuple<std::decay_t<Input>...>, Arguments>;

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
            // Explicit enumSpec supplies its own extrema, even outside the
            // automatic scan. Intervals allow unnamed interior codes; this is
            // not a dictionary-membership test. Check before the enum cast.
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
        // Short-circuit on the first invalid argument. Only local values have
        // changed; no owner call occurs until every conversion succeeds.
        return (convert<I>(metadata, values[I], output) && ...);
    }

    // Native callers never construct Scalar. Normalize each numeric/enum
    // source directly to the target's underlying C++ type, then apply the same
    // descriptor limits as the transport path before any enum cast or callback.
    template <std::size_t I, class Input>
    TELEMETRY_FORCE_INLINE static bool convertNative(
        const Metadata* metadata, Input value, Arguments& output) noexcept
    {
        using T = std::tuple_element_t<I, Arguments>;
        RawNumberT<T> number{};
        if (!convertNumberTo(static_cast<RawNumberT<Input>>(value), number)
            || !validateNative<I>(metadata, number)) return false;
        std::get<I>(output) = static_cast<T>(number);
        return true;
    }

    template <std::size_t... I, class... Input>
    TELEMETRY_FORCE_INLINE static bool convertNativeAll(
        const Metadata* metadata, Arguments& output,
        std::index_sequence<I...>, Input... values) noexcept
    {
        static_assert(sizeof...(I) == sizeof...(Input),
                      "Typed command argument count must match the target signature");
        static_assert((isFactoryValue<Input> && ...),
                      "Typed commands require native numeric or enum values");
        (void) metadata;
        (void) output;
        // A later failure cannot partially apply a command: only this local
        // tuple is modified until every argument has passed its checks.
        return (convertNative<I>(metadata, values, output) && ...);
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

    template <std::size_t I>
    static bool parameterEntry(const void* metadata, void* context, CommandParamSink sink) noexcept
    {
        return sink(context, parameter<I>(static_cast<const Metadata*>(metadata)));
    }

    template <std::size_t... I>
    static constexpr auto parameterEntries(std::index_sequence<I...>) noexcept
    {
        using Emit = bool (*)(const void*, void*, CommandParamSink) noexcept;
        return std::array<Emit, sizeof...(I)>{&parameterEntry<I>...};
    }

    static bool describeParameter(const void* metadata, std::uint32_t index,
                                  void* context, CommandParamSink sink) noexcept
    {
        static constexpr auto entries = parameterEntries(std::make_index_sequence<Traits::arity>{});
        return sink != nullptr && index < entries.size() && entries[index](metadata, context, sink);
    }
    static_assert(Traits::arity <= UINT32_MAX, "Command parameter count exceeds indexed metadata capacity");
    inline static constexpr CommandParamOps parameterOps{
        static_cast<std::uint32_t>(Traits::arity), &schema, &describeParameter};

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
    // Target and signature exist only in the type. The erased descriptor stores
    // the exact owner address and a thunk; no function-pointer reinterpret cast.
    using Traits = CallableTraits<decltype(Target)>;
    using Contract = CommandContract<Traits, Metadata>;
    using Arguments = typename Contract::Arguments;
    static_assert(nonNullTarget<Target>, "Command target cannot be null");
    static_assert(std::is_same_v<typename Traits::Result, CommandResult>,
                  "Command target must return CommandResult");

    template <std::size_t... I>
    static CommandResult run(const void* target, const void* metadata, const Scalar* values,
                             std::index_sequence<I...> sequence) noexcept
    {
        auto* owner = resolveFactoryOwner(static_cast<Owner*>(const_cast<void*>(target)));
        if constexpr (isOwnerSlot<Owner>) {
            if (owner == nullptr) return CommandResult::Unavailable;
        }
        if (!targetAvailable<Target>()) return CommandResult::Unavailable;
        Arguments converted{};
        const auto* definition = static_cast<const Metadata*>(metadata);
        if (!Contract::convertAll(definition, values, converted, sequence))
            return CommandResult::InvalidValue;
        // Invoke the same object selected before argument conversion.
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
        auto* owner = resolveFactoryOwner(static_cast<Owner*>(const_cast<void*>(target)));
        if constexpr (isOwnerSlot<Owner>) {
            if (owner == nullptr) return CommandResult::Unavailable;
        }
        if (!targetAvailable<Target>()) return CommandResult::Unavailable;
        if constexpr (Contract::template exactArguments<Input...>) {
            // Preserve the original direct path, including its stack/codegen.
            if (!Contract::validateNativeAll(metadata, sequence, values...))
                return CommandResult::InvalidValue;
            return invokeFactory<Target>(owner, values...);
        } else {
            Arguments converted{};
            if (!Contract::convertNativeAll(metadata, converted, sequence, values...))
                return CommandResult::InvalidValue;
            return invokeFactory<Target>(owner, std::get<I>(converted)...);
        }
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
        if (name == nullptr) invalidFieldLimits();
        Contract::validateMetadata(metadata);
        return Command{name, owner, metadata, &run, &Contract::parameterOps};
    }
};

template <class Callable, class Metadata = NoCommandArgs>
struct BorrowedCommandBinding {
    // Restore the exact admitted cv-qualified closure type. The closure is
    // borrowed, never copied; even capturing lambdas retain their native state.
    static_assert(hasFactoryCallSignature<Callable>,
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
        auto callable = resolveFactoryCallable(static_cast<Callable*>(const_cast<void*>(target)));
        if constexpr (isCallableSlot<Callable>) {
            if (!callable) return CommandResult::Unavailable;
        }
        Arguments converted{};
        const auto* definition = static_cast<const Metadata*>(metadata);
        if (!Contract::convertAll(definition, values, converted, sequence))
            return CommandResult::InvalidValue;
        return invokeResolvedCallable(callable, std::get<I>(converted)...);
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
        auto callable = resolveFactoryCallable(static_cast<Callable*>(const_cast<void*>(target)));
        if constexpr (isCallableSlot<Callable>) {
            if (!callable) return CommandResult::Unavailable;
        }
        if constexpr (Contract::template exactArguments<Input...>) {
            if (!Contract::validateNativeAll(metadata, sequence, values...))
                return CommandResult::InvalidValue;
            return invokeResolvedCallable(callable, values...);
        } else {
            Arguments converted{};
            if (!Contract::convertNativeAll(metadata, converted, sequence, values...))
                return CommandResult::InvalidValue;
            return invokeResolvedCallable(callable, std::get<I>(converted)...);
        }
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
        if (name == nullptr) invalidFieldLimits();
        Contract::validateMetadata(metadata);
        return Command{name, callable, metadata, &run, &Contract::parameterOps};
    }
};

// Member functions borrow only lvalues; free/static functions need no owner.
template <auto Target, class Owner, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)> && !std::is_reference_v<Owner>, int> = 0>
constexpr Command materializeCommand(const char* name, Owner& owner) noexcept
{
    return detail::CommandBinding<Target, std::remove_reference_t<Owner>>::make(name, std::addressof(owner));
}
template <auto Target, std::enable_if_t<!std::is_member_function_pointer_v<decltype(Target)>, int> = 0>
constexpr Command materializeCommand(const char* name) noexcept
{
    return detail::CommandBinding<Target, detail::NoOwner>::make(name, nullptr);
}
template <auto Target, class Owner, class Metadata, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)> && !std::is_reference_v<Owner>
          && !std::is_reference_v<Metadata> && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command materializeCommand(const char* name, Owner& owner, Metadata& metadata) noexcept
{
    return detail::CommandBinding<Target, std::remove_reference_t<Owner>, std::decay_t<Metadata>>::make(
        name, std::addressof(owner), std::addressof(metadata));
}
template <auto Target, class Metadata, std::enable_if_t<
          !std::is_member_function_pointer_v<decltype(Target)> && !std::is_reference_v<Metadata>
          && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command materializeCommand(const char* name, Metadata& metadata) noexcept
{
    return detail::CommandBinding<Target, detail::NoOwner, std::decay_t<Metadata>>::make(
        name, nullptr, std::addressof(metadata));
}

// These low-level factories borrow metadata as well as the owner. Reject
// rvalues even when explicit const/reference template arguments would otherwise
// make a temporary bind to a const lvalue reference.
template <auto Target, class Owner, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)>, int> = 0>
Command materializeCommand(const char*, Owner&&) = delete;

template <auto Target, class Owner, class Metadata, std::enable_if_t<
          std::is_member_function_pointer_v<decltype(Target)>
          && detail::isCommandArgs<Metadata>, int> = 0>
Command materializeCommand(const char*, Owner&&, Metadata&&) = delete;

template <auto Target, class Metadata, std::enable_if_t<
          !std::is_member_function_pointer_v<decltype(Target)>
          && detail::isCommandArgs<Metadata>, int> = 0>
Command materializeCommand(const char*, Metadata&&) = delete;


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
          && !std::is_reference_v<Metadata>
          && detail::isCommandArgs<Metadata>, int> = 0>
constexpr Command materializeCommand(const char* name, Callable& callable,
                              Metadata& metadata) noexcept
{
    return detail::BorrowedCommandBinding<Callable, std::decay_t<Metadata>>::make(
        name, std::addressof(callable), std::addressof(metadata));
}

template <class Callable, class Metadata, std::enable_if_t<
          std::is_class_v<std::remove_cv_t<Callable>>
          && !std::is_lvalue_reference_v<Metadata>
          && detail::isCommandArgs<Metadata>, int> = 0>
Command materializeCommand(const char*, Callable&, Metadata&&) = delete;

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
