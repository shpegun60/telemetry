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
#include <tuple>

namespace telemetry {

namespace detail {
template <class Limits> struct ArgumentMetadata {
    const char* name;
    const char* unit;
    Limits values;
};
struct NoCommandArgs {};
} // namespace detail

constexpr auto arg(const char* name, const char* unit = "") noexcept
{
    return detail::ArgumentMetadata<detail::NoLimits>{name, unit, {}};
}
template <class T>
constexpr auto arg(const char* name, const char* unit, T initial) noexcept
{
    return detail::ArgumentMetadata<decltype(limits(initial))>{name, unit, limits(initial)};
}
template <class T>
constexpr auto arg(const char* name, const char* unit, T initial, T minimum, T maximum) noexcept
{
    return detail::ArgumentMetadata<decltype(limits(initial, minimum, maximum))>{
        name, unit, limits(initial, minimum, maximum)};
}

template <class... A> struct CommandArgs {
    const std::tuple<A...> entries;
    static constexpr std::size_t count = sizeof...(A);
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
} // namespace detail
} // namespace telemetry
#endif
