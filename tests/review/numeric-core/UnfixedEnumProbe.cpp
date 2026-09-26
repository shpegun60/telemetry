// Review probe (numeric-core): automatic dictionary for an unscoped enum without
// a fixed underlying type (its value range is 0..3 for this enumerator set).
#include "field/TelemetryEnum.h"
enum Color { Red, Green, Blue };
constexpr auto type = telemetry::enumType<Color>();
static_assert(type.enumCount() == 3);
int main() { return 0; }
