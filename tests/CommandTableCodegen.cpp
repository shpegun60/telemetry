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
    telemetry::command<&CommandProbeDevice::reset>(0, "Reset", commandProbeDevice),
    telemetry::command<&CommandProbeDevice::configure>(
        1, "Configure", commandProbeDevice,
        telemetry::arg<1>("Mode", "", CommandProbeMode::Automatic),
        telemetry::arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f))};
} // namespace

extern "C" const telemetry::Command* const telemetry_probe_command_table = commands.data();
extern "C" const std::size_t telemetry_probe_command_count = commands.size();

static_assert(sizeof(telemetry::Command) == sizeof(void*) * 6);
static_assert(commands.size() == 2 && commands.index().find(1) == &commands[1]);
static_assert(!std::is_copy_constructible_v<std::remove_cv_t<decltype(commands)>>);

extern "C" telemetry::CommandResult command_table_call_known(
    float voltage, CommandProbeMode mode) noexcept
{
    return commands.call<1>(voltage, mode);
}

extern "C" telemetry::CommandResult command_table_call_runtime(
    std::size_t index, float voltage, CommandProbeMode mode) noexcept
{
    return commands.call(index, voltage, mode);
}

extern "C" telemetry::CommandResult command_table_execute_erased(
    telemetry::CommandId id, const telemetry::Scalar* values) noexcept
{
    return commands.index().execute(id, values, 2);
}
