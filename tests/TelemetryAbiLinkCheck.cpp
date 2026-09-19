// Link-only check: the caller and compiled telemetry archive must use one Field ABI.
#include "TelemetryJson.h"

int main()
{
    telemetry::requireTelemetryAbi();
    return telemetry::schemaCrc(nullptr, 0) == 2166136261u ? 0 : 1;
}
