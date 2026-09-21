// Compiled adapters must reject a caller with a different telemetry ABI (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <resource/telemetry/TelemetryFiles.hpp>

int main()
{
#if ADAPTER == 0
    telemetry_resource::SchemaFile file{telemetry::CatalogIndex{}};
#elif ADAPTER == 1
    telemetry_resource::CommandsFile file{telemetry::CommandCatalogIndex{}};
#else
    telemetry_resource::ValuesFile file{telemetry::CatalogIndex{}};
#endif
    return file.size() == 0;
}
