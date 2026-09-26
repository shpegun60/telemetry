#include "serialization/TelemetryCommandJson.h"
int main()
{
    // This test isolates ABI linkage, not the minimum schema-buffer capacity.
    char text[512];
    return telemetry::writeSchema(telemetry::CommandIndex{},text,sizeof(text)) != 0 ? 0 : 1;
}
