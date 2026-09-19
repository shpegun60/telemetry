// Link-only check: the umbrella and core ABI anchor need no JSON module.
#include "Telemetry.h"

int main()
{
    telemetry::requireTelemetryAbi();
    return telemetry::telemetryAbiSignature != 0 ? 0 : 1;
}
