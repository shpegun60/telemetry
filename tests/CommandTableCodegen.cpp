// Owning command metadata must remain constexpr storage around ordinary Commands.
#include "Telemetry.h"

namespace {
enum class Mode : std::uint16_t { Off, Automatic, Manual };
struct Device {
    telemetry::CommandResult reset() noexcept
    { return telemetry::CommandResult::Executed; }
    telemetry::CommandResult configure(float, Mode) noexcept
    { return telemetry::CommandResult::Executed; }
};
extern Device device;

constexpr telemetry::CommandTable commands{
    telemetry::command<&Device::reset>(0, "Reset", device),
    telemetry::command<&Device::configure>(
        1, "Configure", device,
        telemetry::arg<1>("Mode", "", Mode::Automatic),
        telemetry::arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f))};
} // namespace

extern "C" const telemetry::Command* const telemetry_probe_command_table = commands.data();
extern "C" const std::size_t telemetry_probe_command_count = commands.size();

static_assert(sizeof(telemetry::Command) == sizeof(void*) * 6);
static_assert(commands.size() == 2 && commands.index().find(1) == &commands[1]);
static_assert(!std::is_copy_constructible_v<std::remove_cv_t<decltype(commands)>>);

extern "C" telemetry::CommandResult command_table_call(float voltage,
                                                         std::uint16_t mode) noexcept
{
    return commands.index().call(1, voltage, mode);
}
