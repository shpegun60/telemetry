// Link-only check: compiled JSON entry points encode the caller's exact Field ABI.
#include "serialization/TelemetryJson.h"

int main()
{
    // Exercise both ABI-tagged entry points without pinning this link fixture
    // to a particular JSON format. TelemetryJsonCheck pins the wire hash.
    return telemetry::schemaCrc(nullptr, 0) == telemetry::schemaCrc(telemetry::CatalogIndex{}) ? 0 : 1;
}
