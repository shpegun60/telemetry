// Review repro (slots): the slot examples from lib/telemetry/README.md
// ("Runtime objects/functions behind constant tables") and
// lib/telemetry/slot/README.md, completed with the minimum declarations,
// with each annotated result checked.
#include "Telemetry.h"
#include <cstdio>
using namespace telemetry;

namespace owner_example {
enum class Mode : std::uint8_t { Off, Auto, Manual };
struct Device {
    float v = 231.f, lim = 250.f;
    Mode m = Mode::Off;
    float voltage() const noexcept { return v; }
    float limit() const noexcept { return lim; }
    WriteResult setLimit(float value) noexcept { lim = value; return WriteResult::Applied; }
    CommandResult calibrate(float voltage, Mode mode) noexcept { v = voltage; m = mode; return CommandResult::Executed; }
};
inline OwnerSlot<Device> deviceSlot; // Empty; one pointer in RAM.
inline constexpr FieldTable lateFields{
    field<&Device::voltage>("Ua", "V", deviceSlot),
    field<&Device::limit, &Device::setLimit>("Limit", "V", deviceSlot,
        limits(250.0f, 1.0f, 1000.0f)),
};
inline constexpr CommandTable lateCommands{
    command<&Device::calibrate>("Calibrate", deviceSlot,
        arg<0>("Voltage", "V", 230.0f, 0.0f, 500.0f)),
};
bool run()
{
    Device device;
    deviceSlot.bind(device);
    auto voltage = lateFields.read<0>();          // optional<float>, no Scalar.
    auto status = lateCommands.call<0>(230.0, 1); // Native checked conversion.
    deviceSlot.reset();                           // Before destroying device.
    static_assert(std::is_same_v<decltype(voltage), std::optional<float>>);
    return voltage == 231.f && status == CommandResult::Executed && device.m == Mode::Auto
        && !lateFields.read<0>();
}
} // namespace owner_example

namespace function_example {
float a = 1.f, b = 2.f;
float readVoltageA() noexcept { return a; }
float readVoltageB() noexcept { return b; }
WriteResult writeVoltageA(float v) noexcept { a = v; return WriteResult::Applied; }
int started = 0;
CommandResult startMotor(float) noexcept { ++started; return CommandResult::Accepted; }
inline FunctionSlot<float() noexcept> voltageRead;
inline FunctionSlot<WriteResult(float) noexcept> voltageWrite;
inline FunctionSlot<CommandResult(float) noexcept> start;
inline constexpr FieldTable selectableFields{
    field("Voltage", "V", voltageRead, voltageWrite),
};
inline constexpr CommandTable selectableCommands{
    command("Start", start, arg<0>("Speed", "rpm", 0.f, 0.f, 3000.f)),
};
bool run()
{
    voltageRead.bind(&readVoltageA);
    voltageWrite.bind(&writeVoltageA);
    start.bind(&startMotor);
    auto voltage = selectableFields.read<0>();       // optional<float>, no Scalar.
    auto status = selectableCommands.call<0>(1500); // int -> float, no Scalar.
    voltageRead.bind(&readVoltageB);                  // Existing table sees B.
    const bool sawB = selectableFields.read<0>() == 2.f;
    voltageRead.reset();                             // Reads now report absence.
    return voltage == 1.f && status == CommandResult::Accepted && sawB && !selectableFields.read<0>();
}
} // namespace function_example

namespace slot_readme_example {
struct Meter {
    float v = 230.f;
    float voltage() const noexcept { return v; }
    WriteResult setLimit(float) noexcept { return WriteResult::Applied; }
    CommandResult configure(float) noexcept { return CommandResult::Executed; }
};
inline ContextFunctionSlot<float() noexcept> contextRead;
inline DelegateRefSlot<float() noexcept> borrowedRead;
inline DelegateSlot<float() noexcept, 32> ownedRead;
inline DelegateSlot<WriteResult(float) noexcept, 32> ownedWrite;
inline DelegateSlot<CommandResult(float) noexcept, 32> configure;

inline constexpr FieldTable fields{
    field("Context", "V", contextRead),
    field("Borrowed", "V", borrowedRead),
    field("Owned", "V", ownedRead, ownedWrite, limits(230.f, 0.f, 500.f)),
};
inline constexpr CommandTable commands{
    command("Configure", configure, arg<0>("Value", "V", 230.f, 0.f, 500.f)),
};
bool run()
{
    static Meter meter;
    contextRead.bind(+[](void* p) noexcept {
        return static_cast<Meter*>(p)->voltage();
    }, &meter);
    borrowedRead.bind<&Meter::voltage>(meter);
    // Or borrow a stable named closure; the closure must outlive its binding.
    static auto getter = [&m = meter]() noexcept { return m.voltage(); };
    borrowedRead.bind(getter);
    ownedRead.bind([correction = 1.02f, &m = meter]() noexcept {
        return m.voltage() * correction;
    });
    ownedWrite.bind([&m = meter](float value) noexcept { return m.setLimit(value); });
    configure.bind([&m = meter](float value) noexcept { return m.configure(value); });

    auto value = fields.read<2>();         // optional<float>; native callback.
    auto write = fields.write<2>(250);     // Checked int -> float conversion.
    auto result = commands.call<0>(250.);  // Checked double -> float; no Scalar array.
    ownedRead.reset();                     // Subsequent reads return nullopt.
    static_assert(std::is_same_v<decltype(value), std::optional<float>>);
    return value == 230.f * 1.02f && write == WriteResult::Applied
        && result == CommandResult::Executed && !fields.read<2>()
        && fields.read<0>() == 230.f && fields.read<1>() == 230.f;
}
} // namespace slot_readme_example

int main()
{
    const bool a = owner_example::run(), b = function_example::run(), c = slot_readme_example::run();
    std::printf("owner README example: %s\nfunction README example: %s\nslot README example: %s\n",
                a ? "ok" : "FAIL", b ? "ok" : "FAIL", c ? "ok" : "FAIL");
    return a && b && c ? 0 : 1;
}
