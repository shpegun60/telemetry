// Review probe (commands slice), compile-only: a plain `char` command parameter
// infers S8 on x86 hosts and U8 on arm-none-eabi (char is unsigned there), so its
// schema "t", bounds and CRC differ although the README's portability list does
// not name char.
#include "Telemetry.h"
telemetry::CommandResult takeChar(char) noexcept { return telemetry::CommandResult::Executed; }
constexpr telemetry::CommandTable rows{telemetry::command<&takeChar>("char")};
#if defined(__arm__)
static_assert(telemetry::Scalar::from(char{}).type() == telemetry::ScalarType::U8);
#else
static_assert(telemetry::Scalar::from(char{}).type() == telemetry::ScalarType::S8);
#endif
int main() {}
