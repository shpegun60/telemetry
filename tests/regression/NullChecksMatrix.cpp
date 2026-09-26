// GCC constexpr pointer-presence matrix (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
// Compile with
// -DCASE=N -fsyntax-only plus -fno-delete-null-pointer-checks or
// -fsanitize=undefined. Cases 1..13 are valid constexpr tables that should
// compile under those flags; cases 20..26 bind a null compile-time target and
// must be REJECTED ("cannot be null").
#include "Telemetry.h"

using namespace telemetry;

struct Device {
    float volts = 1.f;
    float voltage() const noexcept { return volts; }
    WriteResult setVoltage(float v) noexcept { volts = v; return WriteResult::Applied; }
    CommandResult run() noexcept { return CommandResult::Executed; }
};
enum class Mode : std::uint8_t { Off, On };
float readFree() noexcept { return 1.f; }
WriteResult writeFree(float) noexcept { return WriteResult::Applied; }
Scalar readScalarFree() noexcept { return Scalar::fromF32(1.f); }
WriteResult writeScalarFree(const Scalar&) noexcept { return WriteResult::Applied; }
Mode readMode() noexcept { return Mode::On; }
CommandResult runFree() noexcept { return CommandResult::Executed; }
[[maybe_unused]] Device device;
[[maybe_unused]] OwnerSlot<Device> deviceSlot;
[[maybe_unused]] FunctionSlot<float() noexcept> functionSlot;
[[maybe_unused]] DelegateSlot<float() noexcept> delegateSlot;
[[maybe_unused]] DelegateRefSlot<float() noexcept> delegateRefSlot;
[[maybe_unused]] ContextFunctionSlot<float() noexcept> contextSlot;
[[maybe_unused]] int adapter(Device& d) noexcept { return int(d.volts); }

#if CASE == 1
constexpr FieldTable probe{field("V", "", functionSlot)};
#elif CASE == 2
constexpr FieldTable probe{field("V", "", delegateSlot)};
#elif CASE == 3
constexpr FieldTable probe{field("V", "", delegateRefSlot)};
#elif CASE == 4
constexpr FieldTable probe{field("V", "", contextSlot)};
#elif CASE == 5
constexpr FieldTable probe{field<&Device::voltage, &Device::setVoltage>("V", "", deviceSlot)};
#elif CASE == 6
constexpr FieldTable probe{field<&readFree, &writeFree>("V", "")};
#elif CASE == 7
constexpr Getter probe = Getter::bind<&readScalarFree>();
[[maybe_unused]] constexpr Setter probe2 = Setter::bind<&writeScalarFree>();
#elif CASE == 8
constexpr FieldTable probe{field("V", "", &readFree, &writeFree)};       // parameter function pointers
#elif CASE == 9
constexpr Field probe{"V", "", ScalarType::F32, &readScalarFree, &writeScalarFree}; // manual row
#elif CASE == 10
constexpr FieldTable probe{field<&Device::voltage, &Device::setVoltage>("V", "", device)};
#elif CASE == 11
constexpr CommandTable probe{command<&runFree>("run"), command<&Device::run>("run", deviceSlot)};
#elif CASE == 12
constexpr FieldTable probe{field<&readMode>("M", "")};
#elif CASE == 13
constexpr bool bindInConstantEvaluation() noexcept
{
    FunctionSlot<float() noexcept> local;
    local.bind(&readFree);
    return local.available();
}
constexpr bool probe = bindInConstantEvaluation();
static_assert(probe);
#elif CASE == 20
auto probe = field<static_cast<float (*)() noexcept>(nullptr)>("x", "");
#elif CASE == 21
auto probe = Getter::bind<static_cast<Scalar (*)() noexcept>(nullptr)>();
#elif CASE == 22
auto probe = Setter::bind<static_cast<WriteResult (*)(const Scalar&) noexcept>(nullptr)>();
#elif CASE == 23
auto probe = field<static_cast<float (Device::*)() const noexcept>(nullptr)>("x", "", device);
#elif CASE == 24
auto probe = Getter::bindContext<static_cast<int (*)(Device&) noexcept>(nullptr)>(device);
#elif CASE == 25
[[maybe_unused]] constexpr auto definition = command<static_cast<CommandResult (*)() noexcept>(nullptr)>("run");
constexpr CommandTable probe{command<static_cast<CommandResult (*)() noexcept>(nullptr)>("run")};
#elif CASE == 26
auto probe = field<&readFree, static_cast<WriteResult (*)(float) noexcept>(nullptr)>("x", "");
#else
#error "Select CASE 1..13 or 20..26"
#endif
int main() { (void)&probe; }
