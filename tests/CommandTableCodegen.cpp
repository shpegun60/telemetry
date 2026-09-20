// Owning command metadata must remain constexpr storage around ordinary Commands.
#include "Telemetry.h"

enum class CommandProbeMode : std::uint16_t { Off, Automatic, Manual };
struct CommandProbeDevice {
    telemetry::CommandResult reset() noexcept;
    telemetry::CommandResult configure(float, CommandProbeMode) noexcept;
};

extern CommandProbeDevice commandProbeDevice;

namespace {

constexpr telemetry::CommandTable commands{
    telemetry::command<&CommandProbeDevice::reset>("Reset", commandProbeDevice),
    telemetry::command<&CommandProbeDevice::configure>("Configure", commandProbeDevice,
        telemetry::arg<1>("Mode", "", CommandProbeMode::Automatic),
        telemetry::arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f))};
constexpr telemetry::CommandCatalogTable globalCommands{telemetry::group("commands",commands)};
} // namespace

extern "C" const telemetry::Command* const telemetry_probe_command_table = commands.data();
extern "C" const std::size_t telemetry_probe_command_count = commands.size();

static_assert(sizeof(telemetry::Command) == sizeof(void*) * 5);
static_assert(commands.size() == 2 && commands.index().find(1) == &commands[1]);
static_assert(!std::is_copy_constructible_v<std::remove_cv_t<decltype(commands)>>);

enum class ProbeCommand : std::uint64_t { Reset, Configure };
#ifdef TELEMETRY_ENUM_POSITION_PROBE
constexpr auto configurePosition = ProbeCommand::Configure;
#else
constexpr std::size_t configurePosition = 1;
#endif

#ifndef TELEMETRY_COMMAND_CONVERSION_PROBE
extern "C" telemetry::CommandResult command_table_call_known(
    float voltage, CommandProbeMode mode) noexcept
{
    return commands.call<configurePosition>(voltage, mode);
}

extern "C" telemetry::CommandResult command_table_call_runtime(
    std::size_t index, float voltage, CommandProbeMode mode) noexcept
{
    return commands.call(index, voltage, mode);
}

extern "C" telemetry::CommandResult command_table_call_global(
    float voltage, CommandProbeMode mode) noexcept
{
    return globalCommands.call<telemetry::makeId(0,1)>(voltage,mode);
}

extern "C" telemetry::CommandResult command_table_execute_erased(
    telemetry::CommandId id, const telemetry::Scalar* values) noexcept
{
    return commands.index().execute(id, values, 2);
}
#else
// Match the declared float/enum contract, including conversion BEFORE limits.
// Checking the double input against 500.0 first would change rounding behavior.
extern "C" telemetry::CommandResult command_conversion_direct(double voltage, int mode) noexcept
{
    float nativeVoltage{};
    std::uint16_t nativeMode{};
    if (!telemetry::detail::convertNumberTo(voltage, nativeVoltage)
        || !(nativeVoltage >= 0.0f && nativeVoltage <= 500.0f))
        return telemetry::CommandResult::InvalidValue;
    if (!telemetry::detail::convertNumberTo(mode, nativeMode) || nativeMode > 2)
        return telemetry::CommandResult::InvalidValue;
    return commandProbeDevice.configure(nativeVoltage, static_cast<CommandProbeMode>(nativeMode));
}
extern "C" telemetry::CommandResult command_conversion_local(double voltage, int mode) noexcept
{
    return commands.call<configurePosition>(voltage, mode);
}
extern "C" telemetry::CommandResult command_conversion_global(double voltage, int mode) noexcept
{
    return globalCommands.call<telemetry::makeId(0, 1)>(voltage, mode);
}
extern "C" telemetry::CommandResult command_conversion_runtime(
    std::size_t index, double voltage, int mode) noexcept
{
    return commands.call(index, voltage, mode);
}
#endif
