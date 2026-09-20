/**
 * @file TelemetryCommandArgs.h
 * @brief Owned optional command metadata and synchronous parameter description.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_ARGS_H
#define TELEMETRY_COMMAND_ARGS_H

#include "../field/TelemetryLimits.h"
#include "TelemetryCommand.h"
#include <limits>
#include <tuple>

namespace telemetry {

namespace detail {
// The sentinel identifies positional metadata only; it is never an argument
// array index. Indexed metadata can omit parameters and arrive in any order.
inline constexpr std::size_t positionalCommandArgument =
    std::numeric_limits<std::size_t>::max();

template <std::size_t Index, class Limits> struct ArgumentMetadata {
    static constexpr std::size_t index = Index;
    const char* name;
    const char* unit;
    Limits values;
};

template <std::size_t Wanted, std::size_t Position, class... Entries>
struct CommandMetadataPosition;
template <std::size_t Wanted, std::size_t Position>
struct CommandMetadataPosition<Wanted, Position>
    : std::integral_constant<std::size_t, positionalCommandArgument> {};
template <std::size_t Wanted, std::size_t Position, class First, class... Rest>
struct CommandMetadataPosition<Wanted, Position, First, Rest...>
    : std::conditional_t<First::index == Wanted,
          std::integral_constant<std::size_t, Position>,
          CommandMetadataPosition<Wanted, Position + 1, Rest...>> {};

template <std::size_t... Indices> struct UniqueCommandMetadataIndices;
template <> struct UniqueCommandMetadataIndices<> : std::true_type {};
template <std::size_t First, std::size_t... Rest>
struct UniqueCommandMetadataIndices<First, Rest...>
    : std::bool_constant<((First != Rest) && ...)
        && UniqueCommandMetadataIndices<Rest...>::value> {};

struct NoCommandArgs {};
} // namespace detail

template <std::size_t Index = detail::positionalCommandArgument>
constexpr auto arg(const char* name, const char* unit = "") noexcept
{
    return detail::ArgumentMetadata<Index, detail::NoLimits>{name, unit, {}};
}
template <std::size_t Index = detail::positionalCommandArgument, class T>
constexpr auto arg(const char* name, const char* unit, T initial) noexcept
{
    return detail::ArgumentMetadata<Index, decltype(limits(initial))>{
        name, unit, limits(initial)};
}
template <std::size_t Index = detail::positionalCommandArgument, class E, E... Values>
constexpr auto arg(const char* name, const char* unit,
                   detail::EnumSpec<E, Values...> values) noexcept
{
    return detail::ArgumentMetadata<Index, detail::EnumSpec<E, Values...>>{
        name, unit, values};
}
template <std::size_t Index = detail::positionalCommandArgument, class T>
constexpr auto arg(const char* name, const char* unit, T initial, T minimum, T maximum) noexcept
{
    return detail::ArgumentMetadata<Index, decltype(limits(initial, minimum, maximum))>{
        name, unit, limits(initial, minimum, maximum)};
}

template <class... A> struct CommandArgs {
    // Values are owned, labels are borrowed. Immutability preserves the limits
    // validated when CommandTable materializes its descriptors.
    const std::tuple<A...> entries;
    static constexpr std::size_t count = sizeof...(A);
    static constexpr bool positional =
        ((A::index == detail::positionalCommandArgument) && ...);
    static constexpr bool indexed =
        ((A::index != detail::positionalCommandArgument) && ...);
    static constexpr bool indicesUnique =
        detail::UniqueCommandMetadataIndices<A::index...>::value;

    template <std::size_t Index>
    static constexpr std::size_t position = positional ? Index
        : detail::CommandMetadataPosition<Index, 0, A...>::value;

    template <std::size_t Arity>
    static constexpr bool indicesInRange = ((A::index < Arity) && ...);
};

template <class... A>
constexpr auto commandArgs(A... metadata) noexcept
{
    return CommandArgs<A...>{std::tuple<A...>{metadata...}};
}

namespace detail {
template <class T> struct IsCommandArgs : std::false_type {};
template <class... A> struct IsCommandArgs<CommandArgs<A...>> : std::true_type {};
template <class T> inline constexpr bool isCommandArgs = IsCommandArgs<std::decay_t<T>>::value;

template <class T> struct IsArgumentMetadata : std::false_type {};
template <std::size_t I, class Limits>
struct IsArgumentMetadata<ArgumentMetadata<I, Limits>> : std::true_type {};
template <class T> struct IsIndexedArgumentMetadata : std::false_type {};
template <std::size_t I, class Limits>
struct IsIndexedArgumentMetadata<ArgumentMetadata<I, Limits>>
    : std::bool_constant<I != positionalCommandArgument> {};
template <class T>
inline constexpr bool isIndexedArgumentMetadata =
    IsIndexedArgumentMetadata<std::decay_t<T>>::value;

template <class Metadata, std::size_t Index>
struct CommandMetadataSlot {
    // An absent slot must not instantiate tuple_element at the sentinel index.
    static constexpr bool present = false;
    static constexpr std::size_t position = positionalCommandArgument;
};
template <class... A, std::size_t Index>
struct CommandMetadataSlot<CommandArgs<A...>, Index> {
    static constexpr std::size_t position = CommandArgs<A...>::template position<Index>;
    static constexpr bool present = position != positionalCommandArgument;
};
} // namespace detail
} // namespace telemetry
#endif
