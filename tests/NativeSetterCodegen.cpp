// Measure the actual erased native thunks, not just typed FieldTable wrappers.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "field/TelemetryFieldFactory.h"
using namespace telemetry;

float setterReadF32() noexcept;
WriteResult setterWriteF32(float) noexcept;
std::uint8_t setterReadU8() noexcept;
WriteResult setterWriteU8(std::uint8_t) noexcept;
std::uint32_t setterReadU32() noexcept;
WriteResult setterWriteU32(std::uint32_t) noexcept;
bool setterReadBool() noexcept;
WriteResult setterWriteBool(bool) noexcept;

extern "C" {
extern constexpr Field nativeSetterRows[]{
    field("f32", "", &setterReadF32, &setterWriteF32).materialize(),
    field("u8", "", &setterReadU8, &setterWriteU8).materialize(),
    field("u32", "", &setterReadU32, &setterWriteU32).materialize(),
    field("bool", "", &setterReadBool, &setterWriteBool).materialize(),
    Field{"manual", "", ScalarType::F64, &setterReadF32, &setterWriteF32},
};
WriteResult native_setter_erased_write(const Field& field, float value) noexcept
{ return field.write(value); }
WriteResult native_setter_known_write(float value) noexcept
{ return nativeSetterRows[0].write(value); }
WriteResult native_setter_manual_write(double value) noexcept
{ return nativeSetterRows[4].write(value); }
}
