// Compiled adapters must reject a caller with a different telemetry ABI (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#if defined(LEGACY_ABI7)
#include "../abi/LegacyAbi7.h"
// Model the frozen adapter constructor signature from f1cfbd8. This is a
// link-rejection fixture only: a mixed executable must never be produced.
#define CurrentAbiTag LegacyAbi7Tag
#endif
#include <resource/telemetry/TelemetryFiles.hpp>
#if defined(LEGACY_ABI7)
#undef CurrentAbiTag
#endif

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
