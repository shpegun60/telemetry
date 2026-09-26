// Review probe (numeric-core): README says an invalid numericType definition
// "terminates via std::abort when constructed at runtime". No test runs it.
#include "field/TelemetryFieldType.h"
#include <cstdio>
volatile int runtimeDefault = 11;
int main()
{
    std::printf("constructing numericType<float>(11, 0, 10) at runtime\n");
    std::fflush(stdout);
    const auto type = telemetry::numericType<float>(runtimeDefault, 0, 10);
    std::printf("NOT ABORTED: default=%g\n", static_cast<double>(type.defaultValue().get<float>()));
    return 0;
}
