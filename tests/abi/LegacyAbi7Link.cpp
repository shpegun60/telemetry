#include "LegacyAbi7.h"
namespace telemetry::detail {
void requireTelemetryAbi(LegacyAbi7Tag) noexcept;
std::uint32_t schemaCrcAbi(const CatalogIndex&, LegacyAbi7Tag) noexcept;
std::uint32_t commandSchemaCrcAbi(const CommandIndex&, LegacyAbi7Tag) noexcept;
}
int main()
{
#if TELEMETRY_LEGACY_ABI_MODULE == 0
    telemetry::detail::requireTelemetryAbi(telemetry::detail::LegacyAbi7Tag{});
#elif TELEMETRY_LEGACY_ABI_MODULE == 1
    return static_cast<int>(telemetry::detail::schemaCrcAbi(telemetry::CatalogIndex{}, telemetry::detail::LegacyAbi7Tag{}));
#else
    return static_cast<int>(telemetry::detail::commandSchemaCrcAbi(telemetry::CommandIndex{}, telemetry::detail::LegacyAbi7Tag{}));
#endif
}
