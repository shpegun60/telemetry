/**
 * @file TelemetryAbi.h
 * @brief Exact link-time guard for the in-memory telemetry definition ABI.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_ABI_H
#define TELEMETRY_ABI_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../catalog/TelemetryIndex.h"
#include "../command/TelemetryCommandCatalogIndex.h"
#include "../command/TelemetryCommandIndex.h"

namespace telemetry {

// In-memory ABI revision, not a wire-format version.
// Revision 5 replaces Getter's variant with a two-word payload/invoker,
// includes grouped commands, and guards private nested layout as well as every
// public descriptor/index offset. Command retains its revision-4 owner word,
// which can also borrow a stable callable object.
inline constexpr std::uint32_t telemetryAbiVersion = 5;

// std::variant is not standard-layout on every supported standard library.
// Descriptors remain trivially copyable; each supported compiler's
// conditionally-supported offsetof implementation supplies the guarded offsets.
static_assert(std::is_trivially_copyable_v<Scalar> && std::is_trivially_copyable_v<Getter>
              && std::is_trivially_copyable_v<Setter> && std::is_trivially_copyable_v<FieldType>
              && std::is_trivially_copyable_v<Field> && std::is_trivially_copyable_v<Catalog>
              && std::is_trivially_copyable_v<CatalogIndex>
              && std::is_trivially_copyable_v<Command> && std::is_trivially_copyable_v<CommandParam>
              && std::is_trivially_copyable_v<CommandIndex>
              && std::is_trivially_copyable_v<CommandCatalog>
              && std::is_trivially_copyable_v<CommandCatalogIndex>,
              "The telemetry ABI guard requires trivially copyable descriptors and indexes");

namespace detail {
constexpr std::uint64_t appendAbiWord_(std::uint64_t hash, std::uint64_t value) noexcept
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash = (hash ^ static_cast<std::uint8_t>(value >> shift)) * UINT64_C(1099511628211);
    }
    return hash;
}

template <std::uint64_t... Layout>
struct AbiTag {
    static constexpr std::uint64_t signature() noexcept
    {
        std::uint64_t hash = UINT64_C(14695981039346656037);
        ((hash = appendAbiWord_(hash, Layout)), ...);
        return hash;
    }
};

using CurrentAbiTag = AbiTag<
    telemetryAbiVersion,
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
    offsetof(Field, id),
    offsetof(Field, name),
    offsetof(Field, unit),
    offsetof(Field, set),
    offsetof(Field, declaredType),
    sizeof(Catalog),
    alignof(Catalog),
    offsetof(Catalog, id),
    offsetof(Catalog, name),
    offsetof(Catalog, fields),
    offsetof(Catalog, count),
    sizeof(CatalogIndex), alignof(CatalogIndex),
    CatalogIndex::abiCatalogsOffset(), CatalogIndex::abiCountOffset(),
    sizeof(Command), alignof(Command),
    offsetof(Command, id), offsetof(Command, name),
    offsetof(Command, owner), offsetof(Command, metadata),
    offsetof(Command, invoke), offsetof(Command, describe),
    sizeof(CommandParam), alignof(CommandParam),
    offsetof(CommandParam, index), offsetof(CommandParam, name),
    offsetof(CommandParam, unit), offsetof(CommandParam, type),
    sizeof(CommandIndex), alignof(CommandIndex),
    CommandIndex::abiCommandsOffset(), CommandIndex::abiCountOffset(),
    sizeof(CommandCatalog), alignof(CommandCatalog),
    offsetof(CommandCatalog, id), offsetof(CommandCatalog, name),
    offsetof(CommandCatalog, commands), offsetof(CommandCatalog, count),
    sizeof(CommandCatalogIndex), alignof(CommandCatalogIndex),
    CommandCatalogIndex::abiCatalogsOffset(), CommandCatalogIndex::abiCountOffset()>;
} // namespace detail

// Separate executables may intentionally have different signatures. Within
// one executable, every object that exchanges telemetry definitions must agree.
// Both the diagnostic hash and exact link tag come from the same layout tuple.
inline constexpr std::uint64_t telemetryAbiSignature = detail::CurrentAbiTag::signature();

namespace detail {
// Defined by TelemetryAbi.cpp. The tag is part of the link symbol, so a
// differently configured object cannot accidentally satisfy the reference.
void requireTelemetryAbi(CurrentAbiTag) noexcept;
} // namespace detail

template <class Abi = detail::CurrentAbiTag>
inline void requireTelemetryAbi() noexcept
{
    detail::requireTelemetryAbi(Abi{});
}

} // namespace telemetry

#endif
