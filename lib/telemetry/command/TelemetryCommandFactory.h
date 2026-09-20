/**
 * @file TelemetryCommandFactory.h
 * @brief Owning compile-time command metadata with stable Command views.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_FACTORY_H
#define TELEMETRY_COMMAND_FACTORY_H

#include "../detail/TelemetryCommandBinding.h"
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry {
namespace detail {

template <class... Entries>
constexpr auto ownedCommandMetadata(Entries... entries) noexcept
{
    static_assert((isIndexedArgumentMetadata<Entries> && ...),
                  "command(...) metadata must use indexed arg<N>(...)");
    if constexpr (sizeof...(Entries) == 0) return NoCommandArgs{};
    else return CommandArgs<std::decay_t<Entries>...>{
        std::tuple<std::decay_t<Entries>...>{entries...}};
}

template <class... Entries>
constexpr auto ownedCommandMetadata(CommandArgs<Entries...> metadata) noexcept
{ return metadata; }

// Definitions are construction recipes. Their metadata is copied into the
// final table before descriptor pointers are formed; no recipe is borrowed.
template <auto Target, class Owner, class Metadata>
struct OwnedCommandDefinition {
    using MetadataType = Metadata;
    using Binding = CommandBinding<Target, Owner, Metadata>;
    static constexpr bool reserved = false;
    static constexpr std::size_t arity = Binding::Traits::arity;
    template <class... Input>
    static constexpr bool acceptsArguments = sizeof...(Input) == arity
        && (isFactoryValue<Input> && ...);
    const char* name;
    Owner* owner;
    Metadata metadata;

    constexpr Command materialize(const Metadata* stored) const noexcept
    {
        if constexpr (std::is_same_v<Metadata, NoCommandArgs>)
            return CommandBinding<Target, Owner>::make(name, owner);
        else
            return CommandBinding<Target, Owner, Metadata>::make(name, owner, stored);
    }

    template <class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult invokeTyped(
        const void* target, const Metadata* stored, Input... values) noexcept
    {
        if constexpr (!acceptsArguments<Input...>)
            return CommandResult::ArgumentCountMismatch;
        else
            return Binding::callNative(target, stored, values...);
    }
};

template <class Callable, class Metadata>
struct OwnedBorrowedCommandDefinition {
    using MetadataType = Metadata;
    using Binding = BorrowedCommandBinding<Callable, Metadata>;
    static constexpr bool reserved = false;
    static constexpr std::size_t arity = Binding::Traits::arity;
    template <class... Input>
    static constexpr bool acceptsArguments = sizeof...(Input) == arity
        && (isFactoryValue<Input> && ...);
    const char* name;
    Callable* callable;
    Metadata metadata;

    constexpr Command materialize(const Metadata* stored) const noexcept
    {
        if constexpr (std::is_same_v<Metadata, NoCommandArgs>)
            return BorrowedCommandBinding<Callable>::make(name, callable);
        else
            return BorrowedCommandBinding<Callable, Metadata>::make(
                name, callable, stored);
    }

    template <class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult invokeTyped(
        const void* target, const Metadata* stored, Input... values) noexcept
    {
        if constexpr (!acceptsArguments<Input...>)
            return CommandResult::ArgumentCountMismatch;
        else
            return Binding::callNative(target, stored, values...);
    }
};

struct ReservedCommandDefinition {
    // A reserved row consumes a position but can never invoke an owner. Match
    // every native signature so runtime dispatch reports Unavailable consistently.
    using MetadataType = NoCommandArgs;
    static constexpr bool reserved = true;
    static constexpr std::size_t arity = 0;
    template <class...> static constexpr bool acceptsArguments = true;
    NoCommandArgs metadata{};
    constexpr Command materialize(const NoCommandArgs*) const noexcept { return {}; }
    template <class... Input>
    static constexpr CommandResult invokeTyped(const void*, const NoCommandArgs*, Input...) noexcept
    { return CommandResult::Unavailable; }
};

template <class T> struct IsOwnedCommandDefinition : std::false_type {};
template <auto Target, class Owner, class Metadata>
struct IsOwnedCommandDefinition<OwnedCommandDefinition<Target, Owner, Metadata>>
    : std::true_type {};
template <class Callable, class Metadata>
struct IsOwnedCommandDefinition<OwnedBorrowedCommandDefinition<Callable, Metadata>>
    : std::true_type {};
template <> struct IsOwnedCommandDefinition<ReservedCommandDefinition> : std::true_type {};
} // namespace detail

constexpr auto reservedCommand() noexcept { return detail::ReservedCommandDefinition{}; }

// High-level definitions own their metadata values. Owners, callable objects
// and strings remain borrowed. Direct CommandTable construction gives metadata
// a stable address and emits ordinary Command descriptors without changing ABI.
template <auto Target, class Owner, class... Entries,
          std::enable_if_t<std::is_member_function_pointer_v<decltype(Target)>
              && !std::is_reference_v<Owner>, int> = 0>
constexpr auto command(const char* name, Owner& owner,
                       Entries... entries) noexcept
{
    auto metadata = detail::ownedCommandMetadata(entries...);
    using StoredOwner = std::remove_reference_t<Owner>;
    using Metadata = decltype(metadata);
    return detail::OwnedCommandDefinition<Target, StoredOwner, Metadata>{
        name, std::addressof(owner), metadata};
}

// Deduction and explicitly supplied const Owner template arguments must both
// reject temporaries. A forwarding-reference constraint alone is bypassable
// by spelling Owner as const T& and would retain a dangling owner pointer.
template <auto Target, class Owner, class... Entries,
          std::enable_if_t<std::is_member_function_pointer_v<decltype(Target)>, int> = 0>
auto command(const char*, Owner&&, Entries...) = delete;

template <auto Target, class... Entries,
          std::enable_if_t<!std::is_member_function_pointer_v<decltype(Target)>, int> = 0>
constexpr auto command(const char* name, Entries... entries) noexcept
{
    auto metadata = detail::ownedCommandMetadata(entries...);
    using Metadata = decltype(metadata);
    return detail::OwnedCommandDefinition<Target, detail::NoOwner, Metadata>{
        name, nullptr, metadata};
}

template <class Callable, class... Entries,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Callable>>, int> = 0>
constexpr auto command(const char* name, Callable& callable,
                       Entries... entries) noexcept
{
    static_assert(detail::HasConcreteCallOperator<Callable>::value,
                  "Borrowed command callable must have one concrete operator(); generic and overloaded callables are unsupported");
    auto metadata = detail::ownedCommandMetadata(entries...);
    using Metadata = decltype(metadata);
    return detail::OwnedBorrowedCommandDefinition<Callable, Metadata>{
        name, std::addressof(callable), metadata};
}

template <class Callable, class... Entries,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Callable>>
              && !std::is_lvalue_reference_v<Callable>, int> = 0>
auto command(const char*, Callable&&, Entries...) = delete;

} // namespace telemetry
#endif
