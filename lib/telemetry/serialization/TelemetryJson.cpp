/**
 * @file TelemetryJson.cpp
 * @brief Serialize validated catalogs and values into caller-owned JSON buffers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "TelemetryJson.h"

#include "../detail/TelemetryJsonValue.h"
#include "../detail/TelemetryJsonWriter.h"

#include <cstdint>
#include <cstring>
#include <inttypes.h>
#include <string_view>

namespace telemetry {

namespace {

std::uint32_t hash_byte_(std::uint32_t hash, std::uint8_t byte) noexcept
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

std::uint32_t hash_u64_(std::uint32_t hash, std::uint64_t value) noexcept
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash = hash_byte_(hash, static_cast<std::uint8_t>(value >> shift));
    }
    return hash;
}

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
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    std::uint32_t hash = 2166136261u;
    for (std::size_t c = 0u; c < count; ++c) {
        if (catalogs[c].name == nullptr) return 0;
        hash = hash_byte_(hash, 'C');
        hash = hash_byte_(hash, static_cast<std::uint8_t>(c));
        hash = hash_byte_(hash, static_cast<std::uint8_t>(c >> 8));
        hash = fnv1a_(hash, catalogs[c].name);
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            const Field& field = catalogs[c].fields[i];
            if (field.name == nullptr || field.unit == nullptr) return 0;
            hash = hash_byte_(hash, 'F');
            // Explicit byte order, independent of host endianness/padding.
            for (unsigned shift = 0; shift < 32; shift += 8) {
                hash = hash_byte_(hash, static_cast<std::uint8_t>(makeId(static_cast<GroupId>(c), static_cast<FieldOffset>(i)) >> shift));
            }
            hash = fnv1a_(hash, field.name);
            hash = fnv1a_(hash, field.unit);
            hash = fnv1a_(hash, scalarTypeName(field.declaredType));
            hash = hash_byte_(hash, field.set ? 1u : 0u);
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
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    JsonWriter out {buffer, size, options.int64 == JsonInt64Mode::String};
    if (!out.ok()) return 0;
    if (!out.append("{\"schema\":\"%08lx\",\"catalogs\":[",
                    static_cast<unsigned long>(schemaCrcAbi(index, tag)))) return 0;
    for (std::size_t c = 0u; c < count; ++c) {
        if (!out.append("%s{\"id\":%u,\"name\":",
                        (c == 0u) ? "" : ",", static_cast<unsigned>(c))
            || !out.appendRequiredString(catalogs[c].name)
            || !out.append(",\"fields\":[")) return 0;
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            const Field& field = catalogs[c].fields[i];
            const bool hasEnum = field.declaredType.hasEnum();
            if (!out.append("%s{\"i\":%u,\"id\":%" PRIu32 ",\"n\":",
                            (i == 0u) ? "" : ",", static_cast<unsigned>(i), makeId(static_cast<GroupId>(c), static_cast<FieldOffset>(i)))
                || !out.appendRequiredString(field.name) || !out.append(",\"u\":")
                || !out.appendRequiredString(field.unit)
                || !out.append(",\"t\":\"%s\",\"w\":%s", scalarTypeName(field.declaredType),
                               field.set ? "true" : "false")) return 0;
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
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    JsonWriter out {buffer, size, options.int64 == JsonInt64Mode::String};
    if (!out.append("{")) return 0;
    for (std::size_t c = 0u; c < count; ++c) {
        if (catalogs[c].name == nullptr) return 0;
        if ((c != 0u && !out.append(","))
            || !out.appendString(std::string_view{catalogs[c].name})
            || !out.append(":[")) return 0;
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            if (i != 0u && !out.append(",")) return 0;
            if (!appendScalar(out, catalogs[c].fields[i].read())) return 0;
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
