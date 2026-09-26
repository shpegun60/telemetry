// Regression promoted from tests/review/fields/PointerOwnerProbe.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review probe (fields slice): the owner-type checks use std::is_nothrow_invocable,
// whose INVOKE rules also accept pointers, smart pointers and reference_wrapper
// as the object argument. Such an lvalue is then borrowed as the "owner": the
// descriptor stores the address of the pointer variable, dereferences it on
// every call without a null check, and dangles with the pointer variable.
// Compile with -DCASE=N -fsyntax-only; CASE 0 builds the runtime demonstration.
#include "Telemetry.h"
#include <cstdio>
#include <functional>
#include <memory>

using namespace telemetry;

struct Device {
    float volts = 230.0f;
    float voltage() const noexcept { return volts; }
    WriteResult setVoltage(float v) noexcept { volts = v; return WriteResult::Applied; }
    Scalar scalarVoltage() const noexcept { return volts; }
    WriteResult setScalar(const Scalar&) noexcept { return WriteResult::Applied; }
    enum class Mode : std::uint8_t { Off, On } modeValue = Mode::On;
    Mode mode() const noexcept { return modeValue; }
};
[[maybe_unused]] Device device;
[[maybe_unused]] Device* pointer = &device;
[[maybe_unused]] std::unique_ptr<Device> unique;
[[maybe_unused]] std::reference_wrapper<Device> wrapper{device};

#if CASE == 1
auto probe = Getter::bind<&Device::voltage>(pointer);
#elif CASE == 2
auto probe = Setter::bind<&Device::setScalar>(pointer);
#elif CASE == 3
auto probe = field<&Device::voltage>("Ua", "V", pointer);
#elif CASE == 4
auto probe = field<&Device::voltage, &Device::setVoltage>("Ua", "V", pointer);
#elif CASE == 5
auto probe = field<&Device::mode>("Mode", "", pointer);
#elif CASE == 6
auto probe = field<&Device::scalarVoltage>("S", "", ScalarType::F32, pointer);
#elif CASE == 7
auto probe = field<&Device::voltage>("Ua", "V", unique);
#elif CASE == 8
auto probe = field<&Device::voltage>("Ua", "V", wrapper);
#elif CASE == 9
constexpr FieldTable probe{field<&Device::voltage>("Ua", "V", pointer)};
#elif CASE == 0
int main() { const FieldTable good{field<&Device::voltage>("v", "", *pointer)}; return good.read<0>().value_or(0) == 230.f ? 0 : 1; }
#endif
#if CASE != 0
int main() {}
#endif
