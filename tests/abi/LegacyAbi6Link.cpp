// Frozen ABI 6 tuple from 7751891. Field flags use former padding, so the
// remaining reflected layouts match ABI 6 on these supported configurations.
// Compile this caller separately and require ABI 7 archives to reject its symbols.
#include "abi/TelemetryAbi.h"
static_assert(telemetry::telemetryAbiVersion == 7, "Review the legacy fixture for a new ABI");
namespace telemetry::detail {
using LegacyAbi6Tag = AbiTag<
    6,
    cacheLineBytes,
    sizeof(void*),
    sizeof(Scalar),
    alignof(Scalar),
    Scalar::abiStorageOffset(), Scalar::abiStorageSize(), Scalar::abiStorageAlign(),
    sizeof(Getter),
    alignof(Getter),
    Getter::abiPayloadOffset(), Getter::abiInvokeOffset(),
    Getter::abiPayloadSize(), Getter::abiPayloadAlign(),
    sizeof(Setter),
    alignof(Setter),
    Setter::abiPayloadOffset(), Setter::abiInvokeOffset(),
    Setter::abiPayloadSize(), Setter::abiPayloadAlign(),
    sizeof(FieldType),
    alignof(FieldType),
    FieldType::abiValueTypeOffset(), FieldType::abiRestrictedOffset(),
    FieldType::abiBoundsOffset(), FieldType::abiInitialOffset(),
    FieldType::abiDescribeOffset(), FieldType::abiBoundsSize(), FieldType::abiBoundsAlign(),
    sizeof(Field),
    alignof(Field),
    offsetof(Field, get),
    offsetof(Field, readType),
    offsetof(Field, name),
    offsetof(Field, unit),
    offsetof(Field, set),
    offsetof(Field, declaredType),
    sizeof(Catalog),
    alignof(Catalog),
    offsetof(Catalog, name),
    offsetof(Catalog, fields),
    offsetof(Catalog, count),
    sizeof(CatalogIndex), alignof(CatalogIndex),
    CatalogIndex::abiCatalogsOffset(), CatalogIndex::abiCountOffset(),
    sizeof(Command), alignof(Command),
    offsetof(Command, name),
    offsetof(Command, owner), offsetof(Command, metadata),
    offsetof(Command, invoke), offsetof(Command, describe),
    sizeof(CommandParam), alignof(CommandParam),
    offsetof(CommandParam, index), offsetof(CommandParam, name),
    offsetof(CommandParam, unit), offsetof(CommandParam, type),
    sizeof(CommandIndex), alignof(CommandIndex),
    CommandIndex::abiCommandsOffset(), CommandIndex::abiCountOffset(),
    sizeof(CommandCatalog), alignof(CommandCatalog),
    offsetof(CommandCatalog, name),
    offsetof(CommandCatalog, commands), offsetof(CommandCatalog, count),
    sizeof(CommandCatalogIndex), alignof(CommandCatalogIndex),
    CommandCatalogIndex::abiCatalogsOffset(), CommandCatalogIndex::abiCountOffset()>;
void requireTelemetryAbi(LegacyAbi6Tag) noexcept;
std::uint32_t schemaCrcAbi(const CatalogIndex&, LegacyAbi6Tag) noexcept;
std::uint32_t commandSchemaCrcAbi(const CommandIndex&, LegacyAbi6Tag) noexcept;
}
int main()
{
#if TELEMETRY_LEGACY_ABI_MODULE == 0
    telemetry::detail::requireTelemetryAbi(telemetry::detail::LegacyAbi6Tag{});
#elif TELEMETRY_LEGACY_ABI_MODULE == 1
    return static_cast<int>(telemetry::detail::schemaCrcAbi(telemetry::CatalogIndex{}, telemetry::detail::LegacyAbi6Tag{}));
#else
    return static_cast<int>(telemetry::detail::commandSchemaCrcAbi(telemetry::CommandIndex{}, telemetry::detail::LegacyAbi6Tag{}));
#endif
}
