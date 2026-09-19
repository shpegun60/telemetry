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

#include "../catalog/TelemetryCatalog.h"

namespace telemetry {

// In-memory ABI revision, not a wire-format version.
// Revision 3 separates read/write contracts and makes definitions immutable.
inline constexpr std::uint32_t telemetryAbiVersion = 3;

static_assert(std::is_standard_layout_v<Field>,
              "The telemetry ABI guard requires standard-layout Field offsets");
static_assert(std::is_standard_layout_v<Catalog>,
              "The telemetry ABI guard requires standard-layout Catalog offsets");

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
    sizeof(Getter),
    alignof(Getter),
    sizeof(Setter),
    alignof(Setter),
    sizeof(FieldType),
    alignof(FieldType),
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
    offsetof(Catalog, count)>;
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
