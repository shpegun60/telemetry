// Link-only check: compiled JSON entry points encode the caller's exact Field ABI.
#include "TelemetryJson.h"

int main()
{
    return telemetry::schemaCrc(nullptr, 0) == 2166136261u ? 0 : 1;
}
