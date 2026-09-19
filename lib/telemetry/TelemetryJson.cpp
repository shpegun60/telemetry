#include "TelemetryJson.h"

#include <cmath>
#include <cstdio>
#include <inttypes.h>

namespace telemetry {

namespace {

const char* type_name_(const ScalarType type) noexcept
{
    switch (type) {
        case ScalarType::Null: return "null";
        case ScalarType::F32: return "f32";
        case ScalarType::F64: return "f64";
        case ScalarType::U8: return "u8";
        case ScalarType::U16: return "u16";
        case ScalarType::U32: return "u32";
        case ScalarType::S8: return "s8";
        case ScalarType::S16: return "s16";
        case ScalarType::S32: return "s32";
        case ScalarType::U64: return "u64";
        case ScalarType::S64: return "s64";
        case ScalarType::Bool: return "bool";
        default: return "?";
    }
}

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

/* Bounded append; returns false once anything did not fit. */
class Writer {
public:
    Writer(char* const buffer, const std::size_t size) noexcept
        : buffer_(buffer), size_(size) {}

    template <typename... Args>
    bool append(const char* format, Args... args) noexcept
    {
        if (!ok_ || (offset_ >= size_)) {
            ok_ = false;
            return false;
        }
        const int written =
            std::snprintf(buffer_ + offset_, size_ - offset_, format, args...);
        if ((written < 0) || (static_cast<std::size_t>(written) >= (size_ - offset_))) {
            ok_ = false;
            return false;
        }
        offset_ += static_cast<std::size_t>(written);
        return true;
    }

    std::size_t length() const noexcept { return ok_ ? offset_ : 0u; }

private:
    char* buffer_;
    std::size_t size_;
    std::size_t offset_ = 0u;
    bool ok_ = true;
};

void append_value_(Writer& out, const Field& field) noexcept
{
    const Scalar value = field.read();

    if (value.type() == ScalarType::Null) {
        // Field::read already normalizes to declaredType. Null represents an
        // unavailable value or a conversion that cannot satisfy that type.
        (void) out.append("null");
        return;
    }

    switch (value.type()) {
        case ScalarType::F32:
            if (std::isfinite(value.get<float>())) {
                (void) out.append("%.7g", static_cast<double>(value.get<float>()));
            } else {
                (void) out.append("null");
            }
            break;
        case ScalarType::F64:
            if (std::isfinite(value.get<double>())) {
                (void) out.append("%.17g", value.get<double>());
            } else {
                (void) out.append("null");
            }
            break;
        case ScalarType::U8:
            (void) out.append("%u", static_cast<unsigned>(value.get<std::uint8_t>()));
            break;
        case ScalarType::U16:
            (void) out.append("%u", static_cast<unsigned>(value.get<std::uint16_t>()));
            break;
        case ScalarType::U32:
            (void) out.append("%" PRIu32, value.get<std::uint32_t>());
            break;
        case ScalarType::S8:
            (void) out.append("%d", static_cast<int>(value.get<std::int8_t>()));
            break;
        case ScalarType::S16:
            (void) out.append("%d", static_cast<int>(value.get<std::int16_t>()));
            break;
        case ScalarType::S32:
            (void) out.append("%" PRId32, value.get<std::int32_t>());
            break;
        case ScalarType::U64:
            (void) out.append("%" PRIu64, value.get<std::uint64_t>());
            break;
        case ScalarType::S64:
            (void) out.append("%" PRId64, value.get<std::int64_t>());
            break;
        case ScalarType::Bool:
            (void) out.append(value.get<bool>() ? "true" : "false");
            break;
        default:
            (void) out.append("null");
            break;
    }
}

}  // namespace

std::uint32_t schemaCrc(const CatalogIndex& index) noexcept
{
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    std::uint32_t hash = 2166136261u;
    for (std::size_t c = 0u; c < count; ++c) {
        hash = hash_byte_(hash, 'C');
        hash = hash_byte_(hash, static_cast<std::uint8_t>(catalogs[c].id));
        hash = hash_byte_(hash, static_cast<std::uint8_t>(catalogs[c].id >> 8));
        hash = fnv1a_(hash, catalogs[c].name);
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            const Field& field = catalogs[c].fields[i];
            hash = hash_byte_(hash, 'F');
            // Explicit byte order, independent of host endianness/padding.
            for (unsigned shift = 0; shift < 32; shift += 8) {
                hash = hash_byte_(hash, static_cast<std::uint8_t>(field.id >> shift));
            }
            hash = fnv1a_(hash, field.name);
            hash = fnv1a_(hash, field.unit);
            hash = fnv1a_(hash, type_name_(field.declaredType));
            hash = hash_byte_(hash, field.set ? 1u : 0u);
        }
        hash = hash_byte_(hash, 'E');
    }
    return hash;
}

std::size_t writeSchema(const CatalogIndex& index, char* const buffer, const std::size_t size) noexcept
{
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    Writer out {buffer, size};
    (void) out.append("{\"schema\":\"%08lx\",\"catalogs\":[",
                      static_cast<unsigned long>(schemaCrc(index)));
    for (std::size_t c = 0u; c < count; ++c) {
        (void) out.append("%s{\"id\":%u,\"name\":\"%s\",\"fields\":[",
                          (c == 0u) ? "" : ",", static_cast<unsigned>(catalogs[c].id), catalogs[c].name);
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            const Field& field = catalogs[c].fields[i];
            (void) out.append("%s{\"i\":%u,\"id\":%" PRIu32 ",\"n\":\"%s\",\"u\":\"%s\",\"t\":\"%s\",\"w\":%s}",
                              (i == 0u) ? "" : ",", static_cast<unsigned>(i),
                              field.id, field.name, field.unit, type_name_(field.declaredType),
                              field.set ? "true" : "false");
        }
        (void) out.append("]}");
    }
    (void) out.append("]}");
    return out.length();
}

std::size_t writeValues(const CatalogIndex& index, char* const buffer, const std::size_t size) noexcept
{
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    Writer out {buffer, size};
    (void) out.append("{");
    for (std::size_t c = 0u; c < count; ++c) {
        (void) out.append("%s\"%s\":[", (c == 0u) ? "" : ",", catalogs[c].name);
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            if (i != 0u) {
                (void) out.append(",");
            }
            append_value_(out, catalogs[c].fields[i]);
        }
        (void) out.append("]");
    }
    (void) out.append("}");
    return out.length();
}

std::uint32_t schemaCrc(const Catalog* catalogs, std::size_t count) noexcept
{
    return schemaCrc(CatalogIndex{catalogs, count});
}

std::size_t writeSchema(const Catalog* catalogs, std::size_t count,
                        char* buffer, std::size_t size) noexcept
{
    return writeSchema(CatalogIndex{catalogs, count}, buffer, size);
}

std::size_t writeValues(const Catalog* catalogs, std::size_t count,
                        char* buffer, std::size_t size) noexcept
{
    return writeValues(CatalogIndex{catalogs, count}, buffer, size);
}

}  // namespace telemetry
