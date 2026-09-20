// Compile-only Cortex-M7 probe for signature-filtered runtime command dispatch.
// Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "command/TelemetryCommandTable.h"

#include <cstddef>
#include <cstdint>
#include <utility>

enum class CommandScaleMode : std::uint16_t { Off, Automatic, Manual };

struct CommandScaleOwner {
    template <std::size_t I>
    telemetry::CommandResult matching(float, CommandScaleMode) noexcept;

    template <std::size_t I>
    telemetry::CommandResult other(std::uint16_t) noexcept;
};

extern CommandScaleOwner commandScaleOwner;

namespace {

template <std::size_t I>
constexpr auto scaleDefinition() noexcept
{
    if constexpr (I % 16 == 1) {
        return telemetry::command<&CommandScaleOwner::template matching<I>>(
            static_cast<telemetry::CommandId>(I), "Matching", commandScaleOwner);
    } else {
        return telemetry::command<&CommandScaleOwner::template other<I>>(
            static_cast<telemetry::CommandId>(I), "Other", commandScaleOwner);
    }
}

template <std::size_t... I>
constexpr auto scaleTable(std::index_sequence<I...>) noexcept
{
    return telemetry::CommandTable{scaleDefinition<I>()...};
}

constexpr auto scale10 = scaleTable(std::make_index_sequence<10>{});
constexpr auto scale32 = scaleTable(std::make_index_sequence<32>{});
constexpr auto scale100 = scaleTable(std::make_index_sequence<100>{});

} // namespace

extern "C" {

__attribute__((noinline)) telemetry::CommandResult command_scale_10(
    std::size_t index, float value, CommandScaleMode mode) noexcept
{
    return scale10.call(index, value, mode);
}

__attribute__((noinline)) telemetry::CommandResult command_scale_32(
    std::size_t index, float value, CommandScaleMode mode) noexcept
{
    return scale32.call(index, value, mode);
}

__attribute__((noinline)) telemetry::CommandResult command_scale_100(
    std::size_t index, float value, CommandScaleMode mode) noexcept
{
    return scale100.call(index, value, mode);
}

} // extern "C"
