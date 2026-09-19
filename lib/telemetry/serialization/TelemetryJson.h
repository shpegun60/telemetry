/**
 * @file TelemetryJson.h
 * @brief Bounded JSON serialization and an order-sensitive schema fingerprint.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_JSON_H
#define TELEMETRY_JSON_H

#include "../abi/TelemetryAbi.h"
#include "../catalog/TelemetryIndex.h"

#include <cstddef>
#include <cstdint>

namespace telemetry {

enum class JsonInt64Mode : std::uint8_t {
    Number,
    String,
};

struct JsonOptions {
    JsonInt64Mode int64 = JsonInt64Mode::Number;
};
static_assert(sizeof(JsonOptions) == 1 && alignof(JsonOptions) == 1,
              "JsonOptions is a fixed one-byte compiled API contract");

namespace detail {
std::uint32_t schemaCrcAbi(const Catalog* catalogs, std::size_t count, CurrentAbiTag) noexcept;
std::uint32_t schemaCrcAbi(const CatalogIndex& index, CurrentAbiTag) noexcept;
std::size_t writeSchemaAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, CurrentAbiTag) noexcept;
std::size_t writeSchemaAbi(const CatalogIndex& index, char* buffer,
                           std::size_t size, CurrentAbiTag) noexcept;
std::size_t writeSchemaAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, JsonOptions,
                           CurrentAbiTag) noexcept;
std::size_t writeSchemaAbi(const CatalogIndex& index, char* buffer,
                           std::size_t size, JsonOptions, CurrentAbiTag) noexcept;
std::size_t writeValuesAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, CurrentAbiTag) noexcept;
std::size_t writeValuesAbi(const CatalogIndex& index, char* buffer,
                           std::size_t size, CurrentAbiTag) noexcept;
std::size_t writeValuesAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, JsonOptions,
                           CurrentAbiTag) noexcept;
std::size_t writeValuesAbi(const CatalogIndex& index, char* buffer,
                           std::size_t size, JsonOptions, CurrentAbiTag) noexcept;
} // namespace detail

// Order-sensitive FNV-1a schema fingerprint, with string/record boundaries.
// This is a version hint, not a guarantee against hash collisions.
template <class Abi = detail::CurrentAbiTag>
inline std::uint32_t schemaCrc(const Catalog* catalogs, std::size_t count) noexcept
{
    return detail::schemaCrcAbi(catalogs, count, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::uint32_t schemaCrc(const CatalogIndex& index) noexcept
{
    return detail::schemaCrcAbi(index, Abi{});
}

// {"schema":"<hash>","catalogs":[{"id":0,"name":"meter","fields":[
//   {"i":0,"id":0,"n":"Ua","u":"V","t":"f32","w":false,
//    "min":null,"max":null,"default":0},...]}]}
// Catalog id is the group position; field id packs (group << 16) | i.
// w reports setter presence; it also participates in the schema fingerprint.
// Every field exports min, max and default, including read-only fields. Null
// types export null for all three. They participate in the fingerprint and
// describe numeric write limits; reading never validates against those limits.
// For ordinary numeric fields, a null min/max means that endpoint is the native
// bound of t (finite for floats). Each endpoint is compacted independently,
// including an explicitly supplied native bound. Enum and Bool bounds are
// always explicit. Custom bounds and defaults retain their values. Internal
// limits are always exact; compaction adds no read/write checks.
// F32 metadata uses 17 digits so parsing it as double preserves its numeric
// value and advertised endpoints remain writable through checked conversion.
// JsonInt64Mode::String emits U64/S64 bounds and defaults as JSON strings;
// native endpoints compacted to null remain null. The logical schema CRC is
// independent of this selected wire representation.
// Enum-described fields retain their ordinary numeric t and add an "enum"
// object mapping decimal code strings to names, e.g. {"0":"Off","1":"Auto"}.
// Codes and names participate in the fingerprint. Descriptions are generated
// on demand, independently of getters; values and writes stay purely numeric.
// Only accepted contiguous prefixes are serialized. The CatalogIndex
// overload reuses a validated group view without rescanning group IDs.
// Pointer/count overloads create such a view for the duration of the call.
// Names, units and enum labels use UTF-8; JSON special/control bytes are escaped.
// Catalog/field names and units must be non-null, NUL-terminated strings.
// The fingerprint and schema serialization return zero for any null metadata.
// Catalog names must be globally unique; field names unique within a catalog.
// Returns the length excluding the terminator, or 0 on insufficient space
// or a null buffer (regardless of size). A nonempty output buffer always
// remains NUL-terminated. On failure the partial buffer must not be sent.
// Output storage must not overlap metadata, strings or source values.
template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeSchema(const Catalog* catalogs, std::size_t count,
                               char* buffer, std::size_t size) noexcept
{
    return detail::writeSchemaAbi(catalogs, count, buffer, size, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeSchema(const CatalogIndex& index, char* buffer, std::size_t size) noexcept
{
    return detail::writeSchemaAbi(index, buffer, size, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeSchema(const Catalog* catalogs, std::size_t count,
                               char* buffer, std::size_t size, JsonOptions options) noexcept
{
    return detail::writeSchemaAbi(catalogs, count, buffer, size, options, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeSchema(const CatalogIndex& index, char* buffer,
                               std::size_t size, JsonOptions options) noexcept
{
    return detail::writeSchemaAbi(index, buffer, size, options, Abi{});
}

// {"meter":[229.98,13247,null,...],"sensor":[...]}
// Null/empty getters, failed conversions and non-finite floats become JSON null.
// Finite F32/F64 use max_digits10 precision and a JSON decimal point in any
// numeric locale. Do not change the process locale concurrently with calls.
// Getter calls stop at the first output failure; earlier reads are not undone.
// A null catalog name fails before any getter in that catalog is invoked.
// Field names and units are not inspected by value serialization.
// Number mode preserves the original numeric JSON output. JavaScript Number
// cannot represent every U64/S64 outside [-(2^53 - 1), 2^53 - 1]. String mode
// quotes only U64/S64 values and preserves every decimal digit for such clients.
// The caller supplies the buffers, scheduling and any envelope.
template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeValues(const Catalog* catalogs, std::size_t count,
                               char* buffer, std::size_t size) noexcept
{
    return detail::writeValuesAbi(catalogs, count, buffer, size, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeValues(const CatalogIndex& index, char* buffer, std::size_t size) noexcept
{
    return detail::writeValuesAbi(index, buffer, size, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeValues(const Catalog* catalogs, std::size_t count,
                               char* buffer, std::size_t size, JsonOptions options) noexcept
{
    return detail::writeValuesAbi(catalogs, count, buffer, size, options, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeValues(const CatalogIndex& index, char* buffer,
                               std::size_t size, JsonOptions options) noexcept
{
    return detail::writeValuesAbi(index, buffer, size, options, Abi{});
}

} // namespace telemetry

#endif
