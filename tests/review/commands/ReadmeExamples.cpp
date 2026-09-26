// Review probe (commands slice): the command snippets of lib/telemetry/README.md
// ("Signature-inferred fields and commands", slots, lambdas), compiled and run
// with the behaviour the README states for each line.
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>
#include <cstring>

using namespace telemetry;

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* label)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}

enum class Mode : std::uint8_t { Off, Auto, Manual };
struct Device {
    int resets = 0, calibrations = 0;
    float lastVoltage = 0;
    Mode lastMode = Mode::Off;
    float voltage() const noexcept { return 230.0f; }
    CommandResult reset() noexcept { ++resets; return CommandResult::Executed; }
    CommandResult calibrate(float voltage, Mode mode) noexcept
    { ++calibrations; lastVoltage = voltage; lastMode = mode; return CommandResult::Executed; }
};
Device device; // Namespace scope; remains alive at this address.

constexpr CommandTable meterCommands{
    command<&Device::reset>("Reset", device),
    command<&Device::calibrate>("Calibrate", device,
        arg<1>("Mode", "", Mode::Auto),
        arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f)),
};
constexpr CommandCatalogTable commands{group("device", meterCommands)};
constexpr auto commandIndex = commands.index();
enum class MeterCommand : std::size_t { Reset, Calibrate };

OwnerSlot<Device> deviceSlot;
constexpr CommandTable lateCommands{
    command<&Device::calibrate>("Calibrate", deviceSlot,
        arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f)),
};

FunctionSlot<CommandResult(float) noexcept> start;
float started = -1;
CommandResult startMotor(float speed) noexcept { started = speed; return CommandResult::Executed; }
constexpr CommandTable selectableCommands{
    command("Start", start, arg<0>("Speed", "rpm", 0.f, 0.f, 3000.f)),
};

int saves = 0;
CommandResult saveConfig() noexcept { ++saves; return CommandResult::Executed; }
constexpr CommandTable systemCommands{command<&saveConfig>("Save")};
constexpr auto resetLambda = []() noexcept { return device.reset(); };
constexpr CommandTable lambdaCommands{command("Reset lambda", resetLambda)};
} // namespace

int main()
{
    // README 94 / 99 / 105 / 108 / 122.
    expect(meterCommands.call<1>(230.0f, Mode::Auto) == CommandResult::Executed, "call<1>(float, Mode)");
    expect(commands.call<makeId(0, 1)>(230.0f, Mode::Auto) == CommandResult::Executed, "global typed");
    const Scalar arguments[] = {230.0f, std::uint8_t{1}};
    expect(commandIndex.execute(makeId(0, 1), arguments, 2) == CommandResult::Executed
           && device.lastMode == Mode::Auto, "transport Scalars");
    expect(meterCommands.call(std::size_t{1}, 230.0f, Mode::Auto) == CommandResult::Executed, "runtime position");
    expect(meterCommands.call<MeterCommand::Calibrate>(230.0, 1) == CommandResult::Executed
           && device.lastMode == Mode::Auto, "enum position, double/int");
    // README 159: lateCommands.call<0>(230.0, 1)
    expect(lateCommands.call<0>(230.0, 1) == CommandResult::Unavailable, "unbound slot");
    deviceSlot.bind(device);
    expect(lateCommands.call<0>(230.0, 1) == CommandResult::Executed, "bound slot, native checked conversion");
    deviceSlot.reset();
    // README 213: selectableCommands.call<0>(1500)
    expect(selectableCommands.call<0>(1500) == CommandResult::Unavailable, "unbound function slot");
    start.bind(&startMotor);
    expect(selectableCommands.call<0>(1500) == CommandResult::Executed && started == 1500.0f, "int -> float");
    expect(selectableCommands.call<0>(3001) == CommandResult::InvalidValue, "limit on function slot");
    // README 339-343.
    expect(systemCommands.call<0>() == CommandResult::Executed && saves == 1, "free function");
    const int before = device.resets;
    expect(lambdaCommands.call<0>() == CommandResult::Executed && device.resets == before + 1, "named lambda");
    expect(lambdaCommands.index().call(0) == CommandResult::Executed, "named lambda erased");

    // README 448-452: grouped JSON shape.
    char json[2048];
    const auto n = writeSchema(commandIndex, json, sizeof json);
    expect(n != 0 && std::strstr(json, "\"commandCatalogs\":[{\"id\":0,\"name\":\"device\",\"commands\":[") != nullptr,
           "grouped schema envelope");
    std::printf("%s\n", json);
    std::printf("%d/%d README command checks passed\n", checks - failures, checks);
    return failures != 0;
}
