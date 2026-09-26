// Review probe (fields slice): pin the documented ARM32 Field layout.
// Compile with the CubeIDE arm-none-eabi-g++ and the run_arm_checks.py flags.
// With -DEXPECT64 it checks the forced 64-byte line layout instead.
#include "Telemetry.h"
#include <cstddef>

using namespace telemetry;

static_assert(sizeof(void*) == 4, "ARM32 probe");
static_assert(sizeof(Getter) == 8 && alignof(Getter) == 4);
static_assert(sizeof(Setter) == 8 && alignof(Setter) == 4);
static_assert(Getter::abiPayloadOffset() == 0 && Getter::abiInvokeOffset() == 4);
static_assert(Setter::abiPayloadOffset() == 0 && Setter::abiInvokeOffset() == 4);
static_assert(sizeof(Scalar) == 16 && alignof(Scalar) == 8);
static_assert(sizeof(FieldType) == 48 && alignof(FieldType) == 8);
static_assert(FieldType::abiValueTypeOffset() == 0 && FieldType::abiRestrictedOffset() == 1
              && FieldType::abiBoundsOffset() == 8 && FieldType::abiInitialOffset() == 24
              && FieldType::abiEnumOpsOffset() == 40);
static_assert(sizeof(FieldFlags) == 4);
#ifndef EXPECT64
static_assert(sizeof(Field) == 96 && alignof(Field) == 32);
static_assert(offsetof(Field, get) == 0 && offsetof(Field, readType) == 8);
static_assert(offsetof(Field, name) == 12 && offsetof(Field, unit) == 16);
static_assert(Field::abiFlagsOffset() == 20);
static_assert(offsetof(Field, set) == 32 && offsetof(Field, declaredType) == 40);
// Line 1 = [32, 64): Setter + valueType/restricted/bounds; default at 64.
static_assert(offsetof(Field, declaredType) + FieldType::abiInitialOffset() == 64);
static_assert(offsetof(Field, declaredType) + FieldType::abiEnumOpsOffset() == 80);
static_assert(sizeof(Field[20]) == 1920);
#else
static_assert(sizeof(Field) == 128 && alignof(Field) == 64);
static_assert(offsetof(Field, set) == 64 && offsetof(Field, declaredType) == 72);
#endif
extern "C" const std::size_t layout_probe[] = {
    sizeof(Field), alignof(Field), offsetof(Field, get), offsetof(Field, readType),
    offsetof(Field, name), offsetof(Field, unit), Field::abiFlagsOffset(),
    offsetof(Field, set), offsetof(Field, declaredType)};
