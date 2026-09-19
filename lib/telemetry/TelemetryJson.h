/**
 * @file TelemetryJson.h
 * @brief Bounded JSON serialization and an order-sensitive schema fingerprint.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 */
#ifndef TELEMETRY_JSON_H
#define TELEMETRY_JSON_H

#include "TelemetryIndex.h"

namespace telemetry {

// Order-sensitive FNV-1a schema fingerprint, with string/record boundaries.
// This is a version hint, not a guarantee against hash collisions.
std::uint32_t schemaCrc(const Catalog* catalogs, std::size_t count) noexcept;
std::uint32_t schemaCrc(const CatalogIndex& index) noexcept;

// {"schema":"<hash>","catalogs":[{"id":0,"name":"meter","fields":[
//   {"i":0,"id":0,"n":"Ua","u":"V","t":"f32","w":false},...]}]}
// Catalog id is the group position; field id packs (group << 16) | i.
// w reports setter presence; it also participates in the schema fingerprint.
// Only accepted contiguous prefixes are serialized. The CatalogIndex
// overload reuses a validated group view without rescanning group IDs.
// Pointer/count overloads create such a view for the duration of the call.
// Names must be ASCII identifiers; units must not contain JSON quotes,
// backslashes or control characters. All strings must be non-null.
// Returns the length excluding the terminator, or 0 on insufficient space
// or a null buffer (regardless of size). A nonempty output buffer always
// remains NUL-terminated. On failure the partial buffer must not be sent.
// Output storage must not overlap metadata, strings or source values.
std::size_t writeSchema(const Catalog* catalogs, std::size_t count,
                        char* buffer, std::size_t size) noexcept;
std::size_t writeSchema(const CatalogIndex& index, char* buffer, std::size_t size) noexcept;

// {"meter":[229.98,13247,null,...],"sensor":[...]}
// Null/empty getters, failed conversions and non-finite floats become JSON null.
// Finite F32/F64 use max_digits10 precision and a JSON decimal point in any
// numeric locale. Do not change the process locale concurrently with calls.
// Getter calls stop at the first output failure; earlier reads are not undone.
// All integers are exact in the text. JavaScript Number cannot represent
// every U64/S64 integer outside [-(2^53 - 1), 2^53 - 1].
// The caller supplies the buffers, scheduling and any envelope.
std::size_t writeValues(const Catalog* catalogs, std::size_t count,
                        char* buffer, std::size_t size) noexcept;
std::size_t writeValues(const CatalogIndex& index, char* buffer, std::size_t size) noexcept;

} // namespace telemetry

#endif
