/**
 * @file TelemetryJson.cpp
 * @brief Serialize validated catalogs and values into caller-owned JSON buffers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "TelemetryJson.h"

#include "../detail/TelemetryJsonValue.h"
#include "../detail/TelemetryJsonWriter.h"
#include "../../magic_enum/magic_enum.hpp"

#include <cstdint>
#include <cstring>
#include <inttypes.h>
#include <string_view>

namespace telemetry {

namespace {

// Flags mode scans named powers of two across all 32 underlying bits, rather
// than magic_enum's ordinary small-integer range. No parallel name list or
// runtime reflection is needed; names retain their explicit string_view length.
constexpr auto fieldFlagEntries = magic_enum::enum_entries<FieldFlag, magic_enum::as_flags<>>();

// Hash the logical schema, never pointer addresses, object padding or current
// measurements. Integer words have a specified byte order; float object bits
// are copied without aliasing before that same byte-order projection.
constexpr std::uint32_t hash_byte_(std::uint32_t hash, std::uint8_t byte) noexcept
{
    return (hash ^ byte) * 16777619u;
}

std::uint32_t fnv1a_(std::uint32_t hash, const char* text) noexcept
{
    for (const char* p = text; *p != '\0'; ++p) {
        hash = hash_byte_(hash, static_cast<std::uint8_t>(*p));
    }
    // Include the string boundary: "a"/"bc" differs from "ab"/"c".
    return hash_byte_(hash, 0u);
}

constexpr std::uint32_t hash_u64_(std::uint32_t hash, std::uint64_t value) noexcept
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash = hash_byte_(hash, static_cast<std::uint8_t>(value >> shift));
    }
    return hash;
}

// The header is immutable. Its fingerprint prefix is fully evaluated by the
// compiler, so exporting a schema never rehashes version or flag-name strings.
constexpr std::uint32_t schemaHeaderHash = []() constexpr noexcept {
    std::uint32_t hash = hash_u64_(hash_byte_(2166136261u, 'S'), jsonSchemaFormatVersion);
    hash = hash_byte_(hash, 'P');
    for (const auto& [flag, name] : fieldFlagEntries) {
        hash = hash_u64_(hash, static_cast<std::uint32_t>(flag));
        hash = hash_u64_(hash, name.size());
        for (const char ch : name) hash = hash_byte_(hash, static_cast<std::uint8_t>(ch));
    }
    return hash_byte_(hash, 'p');
}();

std::uint32_t hash_scalar_(std::uint32_t hash, const Scalar& value) noexcept
{
    hash = hash_byte_(hash, static_cast<std::uint8_t>(value.type()));
    if (value.type() == ScalarType::Null) return hash;
    if (value.type() == ScalarType::F32) {
        const float number = value.get<float>();
        std::uint32_t bits;
        std::memcpy(&bits, &number, sizeof(bits));
        return hash_u64_(hash, bits);
    }
    if (value.type() == ScalarType::F64) {
        const double number = value.get<double>();
        std::uint64_t bits;
        std::memcpy(&bits, &number, sizeof(bits));
        return hash_u64_(hash, bits);
    }
    const auto signedValue = convertScalar<std::int64_t>(value);
    return hash_u64_(hash, signedValue ? static_cast<std::uint64_t>(*signedValue)
                                       : value.get<std::uint64_t>());
}

bool hash_enum_entry_(void* context, const Scalar& value, std::string_view name) noexcept
{
    auto& hash = *static_cast<std::uint32_t*>(context);
    const auto signedCode = convertScalar<std::int64_t>(value);
    const auto bits = signedCode ? static_cast<std::uint64_t>(*signedCode)
                                 : value.get<std::uint64_t>();
    hash = hash_byte_(hash, 'V');
    hash = hash_u64_(hash, bits);
    // Length also separates custom names containing embedded NUL characters.
    hash = hash_u64_(hash, static_cast<std::uint64_t>(name.size()));
    for (char byte : name) hash = hash_byte_(hash, static_cast<std::uint8_t>(byte));
    return true;
}

} // namespace

namespace detail {

std::uint32_t schemaCrcAbi(const CatalogIndex& index, CurrentAbiTag) noexcept
{
    std::uint32_t hash = schemaHeaderHash;
    for (const auto catalog : index.catalogs()) {
        if (catalog.name() == nullptr) return 0;
        hash = hash_byte_(hash, 'C');
        hash = hash_byte_(hash, static_cast<std::uint8_t>(catalog.index()));
        hash = hash_byte_(hash, static_cast<std::uint8_t>(catalog.index() >> 8));
        hash = fnv1a_(hash, catalog.name());
        for (const auto entry : catalog.fields()) {
            const Field& field = entry.field();
            if (field.name == nullptr || field.unit == nullptr) return 0;
            hash = hash_byte_(hash, 'F');
            // Explicit byte order, independent of host endianness/padding.
            for (unsigned shift = 0; shift < 32; shift += 8) {
                hash = hash_byte_(hash, static_cast<std::uint8_t>(entry.id() >> shift));
            }
            hash = fnv1a_(hash, field.name);
            hash = fnv1a_(hash, field.unit);
            hash = fnv1a_(hash, scalarTypeName(field.declaredType));
            hash = hash_byte_(hash, field.writable() ? 1u : 0u);
            // All policy bits, including unknown ones, use fixed little-endian
            // byte order. This fingerprints metadata, not a persistence format.
            for (unsigned shift = 0; shift < 32; shift += 8)
                hash = hash_byte_(hash, static_cast<std::uint8_t>(field.flags().value() >> shift));
            // Revise the format marker for native numeric bounds as null.
            // Continue hashing the actual bounds, independent of their text.
            hash = hash_byte_(hash, 'B');
            hash = hash_scalar_(hash, field.declaredType.minimum());
            hash = hash_scalar_(hash, field.declaredType.maximum());
            hash = hash_scalar_(hash, field.declaredType.defaultValue());
            if (field.declaredType.hasEnum()) {
                hash = hash_byte_(hash, 'D');
                (void) field.declaredType.describeEnum(&hash, &hash_enum_entry_);
                hash = hash_byte_(hash, 'd');
            }
        }
        hash = hash_byte_(hash, 'E');
    }
    return hash;
}

namespace {

std::size_t writeSchemaWithOptions_(const CatalogIndex& index, char* const buffer,
                                    const std::size_t size, JsonOptions options,
                                    CurrentAbiTag tag) noexcept
{
    // Schema traversal only touches immutable definitions. An enum description
    // emits directly into this bounded writer; no temporary dictionary array
    // and no source getter are needed.
    JsonWriter out {buffer, size, options.int64 == JsonInt64Mode::String};
    if (!out.ok()) return 0;
    if (!out.append("{\"schema\":\"%08lx\",\"meta\":{\"formatVersion\":%" PRIu32
                    ",\"fieldFlags\":{\"type\":\"u32\",\"values\":{",
                    static_cast<unsigned long>(schemaCrcAbi(index, tag)), jsonSchemaFormatVersion)) return 0;
    bool firstFlag = true;
    for (const auto& [flag, name] : fieldFlagEntries) {
        if (!out.append("%s\"%" PRIu32 "\":", firstFlag ? "" : ",", static_cast<std::uint32_t>(flag))
            || !out.appendString(name)) return 0;
        firstFlag = false;
    }
    if (!out.append("}}},\"catalogs\":[")) return 0;
    for (const auto catalog : index.catalogs()) {
        if (!out.append("%s{\"id\":%u,\"name\":",
                        catalog.index() == 0u ? "" : ",", static_cast<unsigned>(catalog.index()))
            || !out.appendRequiredString(catalog.name())
            || !out.append(",\"fields\":[")) return 0;
        for (const auto entry : catalog.fields()) {
            const Field& field = entry.field();
            const bool hasEnum = field.declaredType.hasEnum();
            if (!out.append("%s{\"i\":%u,\"id\":%" PRIu32 ",\"n\":",
                            entry.index() == 0u ? "" : ",", static_cast<unsigned>(entry.index()), entry.id())
                || !out.appendRequiredString(field.name) || !out.append(",\"u\":")
                || !out.appendRequiredString(field.unit)
                || !out.append(",\"t\":\"%s\",\"w\":%s,\"f\":%" PRIu32, scalarTypeName(field.declaredType),
                               field.writable() ? "true" : "false", field.flags().value())) return 0;
            if (!out.append(",\"min\":")
                || !appendBound(out, field.declaredType.minimum(), true, hasEnum)
                || !out.append(",\"max\":")
                || !appendBound(out, field.declaredType.maximum(), false, hasEnum)
                || !out.append(",\"default\":")
                || !appendMetadata(out, field.declaredType.defaultValue())) return 0;
            if (hasEnum) {
                if (!out.append(",\"enum\":{")) return 0;
                EnumJsonContext context{out};
                if (!field.declaredType.describeEnum(&context, &appendEnumEntry)) return 0;
                if (!out.append("}")) return 0;
            }
            if (!out.append("}")) return 0;
        }
        if (!out.append("]}")) return 0;
    }
    (void) out.append("]}");
    return out.length();
}

std::size_t writeValuesWithOptions_(const CatalogIndex& index, char* const buffer,
                                    const std::size_t size, JsonOptions options) noexcept
{
    // Read each reached field once, after its catalog prefix has fitted.
    // This is a sequence of reads, not an atomic snapshot across owners; the
    // application supplies synchronization when values must be coherent.
    JsonWriter out {buffer, size, options.int64 == JsonInt64Mode::String};
    if (!out.append("{")) return 0;
    // Values carry no IDs, so raw range iteration needs no indexed views.
    bool firstCatalog = true;
    for (const Catalog& catalog : index) {
        if (catalog.name == nullptr) return 0;
        if ((!firstCatalog && !out.append(","))
            || !out.appendString(std::string_view{catalog.name})
            || !out.append(":[")) return 0;
        firstCatalog = false;
        bool firstField = true;
        for (const Field& field : catalog) {
            if (!firstField && !out.append(",")) return 0;
            firstField = false;
            if (!appendScalar(out, field.read())) return 0;
        }
        if (!out.append("]")) return 0;
    }
    (void) out.append("}");
    return out.length();
}

} // namespace

std::size_t writeSchemaAbi(const CatalogIndex& index, char* const buffer,
                           const std::size_t size, CurrentAbiTag tag) noexcept
{
    return writeSchemaWithOptions_(index, buffer, size, JsonOptions{}, tag);
}

std::size_t writeSchemaAbi(const CatalogIndex& index, char* const buffer,
                           const std::size_t size, JsonOptions options,
                           CurrentAbiTag tag) noexcept
{
    return writeSchemaWithOptions_(index, buffer, size, options, tag);
}

std::size_t writeValuesAbi(const CatalogIndex& index, char* const buffer,
                           const std::size_t size, CurrentAbiTag) noexcept
{
    return writeValuesWithOptions_(index, buffer, size, JsonOptions{});
}

std::size_t writeValuesAbi(const CatalogIndex& index, char* const buffer,
                           const std::size_t size, JsonOptions options,
                           CurrentAbiTag) noexcept
{
    return writeValuesWithOptions_(index, buffer, size, options);
}

std::uint32_t schemaCrcAbi(const Catalog* catalogs, std::size_t count,
                           CurrentAbiTag tag) noexcept
{
    return schemaCrcAbi(CatalogIndex{catalogs, count}, tag);
}

std::size_t writeSchemaAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, CurrentAbiTag tag) noexcept
{
    return writeSchemaAbi(CatalogIndex{catalogs, count}, buffer, size, tag);
}

std::size_t writeSchemaAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, JsonOptions options,
                           CurrentAbiTag tag) noexcept
{
    return writeSchemaAbi(CatalogIndex{catalogs, count}, buffer, size, options, tag);
}

std::size_t writeValuesAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, CurrentAbiTag tag) noexcept
{
    return writeValuesAbi(CatalogIndex{catalogs, count}, buffer, size, tag);
}

std::size_t writeValuesAbi(const Catalog* catalogs, std::size_t count,
                           char* buffer, std::size_t size, JsonOptions options,
                           CurrentAbiTag tag) noexcept
{
    return writeValuesAbi(CatalogIndex{catalogs, count}, buffer, size, options, tag);
}

} // namespace detail
} // namespace telemetry
