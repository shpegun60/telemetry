// Review probe (commands slice): headroom of the 64-byte buffer used by
// tests/TelemetryCommandAbiLinkCheck.cpp, whose non-zero return fails the ABI run.
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>

int main()
{
    char text[256];
    const auto n = telemetry::writeSchema(telemetry::CommandIndex{}, text, sizeof text);
    std::printf("%zu bytes + NUL: %s\n", n, text);
    char small[63];
    std::printf("63-byte buffer -> %zu\n", telemetry::writeSchema(telemetry::CommandIndex{}, small, sizeof small));
    return 0;
}
