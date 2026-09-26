// Critic trial: prints ARM32 descriptor sizes through deliberate static_assert failures.
// Run: arm-none-eabi-g++ -std=c++17 <Cortex-M7 flags> -Ilib/telemetry [-DTELEMETRY_FORCE_CACHELINE=N] -fsyntax-only
// and read the "comparison reduces to (X == 1)" notes; X is the size. Expected to fail compilation.
#include "Telemetry.h"
static_assert(sizeof(telemetry::Field) == 1 && alignof(telemetry::Field) == 1);
static_assert(sizeof(telemetry::FieldType) == 1);
static_assert(sizeof(telemetry::Command) == 1);
static_assert(sizeof(telemetry::Catalog) == 1);
static_assert(sizeof(telemetry::Scalar) == 1);
