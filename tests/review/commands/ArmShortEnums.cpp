// Review probe (commands slice), compile-only: an unscoped enum without a fixed
// underlying type (like tests/TelemetryCommandCheck.cpp's `Legacy`) changes its
// Scalar type between the host and arm-none-eabi (AAPCS short enums), so the
// same command source publishes a different "t" and schema CRC.
#include "Telemetry.h"
enum Legacy { Low = -2, High = 2 };
#if defined(__arm__)
static_assert(sizeof(Legacy) == 1, "arm-none-eabi uses short enums by default");
static_assert(telemetry::Scalar::from(std::underlying_type_t<Legacy>{}).type() == telemetry::ScalarType::S8);
#else
static_assert(telemetry::Scalar::from(std::underlying_type_t<Legacy>{}).type() == telemetry::ScalarType::S32
              || telemetry::Scalar::from(std::underlying_type_t<Legacy>{}).type() == telemetry::ScalarType::U32);
#endif
int main() {}
