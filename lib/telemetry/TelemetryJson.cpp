/**
 * @file TelemetryJson.cpp
 * @brief Serialize validated catalogs and values into caller-owned JSON buffers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 */
#include "TelemetryJson.h"

#include <cmath>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <limits>

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
        : buffer_(buffer), size_(size), ok_(buffer != nullptr && size != 0)
    {
        if (ok_) buffer_[0] = '\0';
    }

    bool append(const char* text) noexcept
    {
        return appendText_(text, std::strlen(text));
    }

    template <typename First, typename... Args>
    bool append(const char* format, First first, Args... args) noexcept
    {
        if (!ok_ || (offset_ >= size_)) {
            ok_ = false;
            return false;
        }
        const int written =
            std::snprintf(buffer_ + offset_, size_ - offset_, format, first, args...);
        if ((written < 0) || (static_cast<std::size_t>(written) >= (size_ - offset_))) {
            buffer_[offset_] = '\0';
            ok_ = false;
            return false;
        }
        offset_ += static_cast<std::size_t>(written);
        return true;
    }

    bool appendFloating(double number, int precision) noexcept
    {
        if (!ok_) return false;
        // %g uses the current decimal separator. Format locally, then replace
        // that separator without changing the application's numeric locale.
        char text[64];
        const int written = std::snprintf(text, sizeof(text), "%.*g", precision, number);
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(text)) {
            ok_ = false;
            return false;
        }
        const char* const decimal = std::localeconv()->decimal_point;
        if (std::strcmp(decimal, ".") != 0 && decimal[0] != '\0') {
            if (const char* const point = std::strstr(text, decimal)) {
                return appendText_(text, static_cast<std::size_t>(point - text))
                    && append(".") && append(point + std::strlen(decimal));
            }
        }
        return appendText_(text, static_cast<std::size_t>(written));
    }

    bool appendInteger(std::uint64_t number) noexcept { return appendDecimal_(number, false); }

    bool appendInteger(std::int64_t number) noexcept
    {
        const bool negative = number < 0;
        // Unsigned subtraction also handles INT64_MIN without signed overflow.
        const auto bits = static_cast<std::uint64_t>(number);
        return appendDecimal_(negative ? std::uint64_t{0} - bits : bits, negative);
    }

    bool appendString(std::string_view text) noexcept
    {
        if (!append("\"")) return false;
        std::size_t first = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte >= 0x20 && byte != '"' && byte != '\\') continue;
            if (i != first && !appendText_(text.data() + first, i - first)) return false;
            if (byte == '"' || byte == '\\') {
                if (!append("\\%c", static_cast<int>(byte))) return false;
            } else if (!append("\\u%04x", static_cast<unsigned>(byte))) return false;
            first = i + 1;
        }
        if (first != text.size() && !appendText_(text.data() + first, text.size() - first)) return false;
        return append("\"");
    }

    bool ok() const noexcept { return ok_; }
    std::size_t length() const noexcept { return ok_ ? offset_ : 0u; }

private:
    bool appendDecimal_(std::uint64_t number, bool negative) noexcept
    {
        // Newlib-nano need not support printf's long-long format. Keep the
        // full integer value independent of that optional C library feature.
        char text[std::numeric_limits<std::uint64_t>::digits10 + 2];
        char* const end = text + sizeof(text);
        char* first = end;
        do {
            *--first = static_cast<char>('0' + number % 10);
            number /= 10;
        } while (number != 0);
        if (negative) *--first = '-';
        return appendText_(first, static_cast<std::size_t>(end - first));
    }

    bool appendText_(const char* text, std::size_t length) noexcept
    {
        if (!ok_ || length >= size_ - offset_) {
            ok_ = false;
            return false;
        }
        std::memcpy(buffer_ + offset_, text, length);
        offset_ += length;
        buffer_[offset_] = '\0';
        return true;
    }

    char* buffer_;
    std::size_t size_;
    std::size_t offset_ = 0u;
    bool ok_;
};

bool append_value_(Writer& out, const Field& field) noexcept
{
    const Scalar value = field.read();

    if (value.type() == ScalarType::Null) {
        // Field::read already normalizes to declaredType. Null represents an
        // unavailable value or a conversion that cannot satisfy that type.
        return out.append("null");
    }

    switch (value.type()) {
        case ScalarType::F32:
            if (std::isfinite(value.get<float>())) {
                return out.appendFloating(value.get<float>(), std::numeric_limits<float>::max_digits10);
            }
            return out.append("null");
        case ScalarType::F64:
            if (std::isfinite(value.get<double>())) {
                return out.appendFloating(value.get<double>(), std::numeric_limits<double>::max_digits10);
            }
            return out.append("null");
        case ScalarType::U8:
            return out.append("%u", static_cast<unsigned>(value.get<std::uint8_t>()));
        case ScalarType::U16:
            return out.append("%u", static_cast<unsigned>(value.get<std::uint16_t>()));
        case ScalarType::U32:
            return out.append("%" PRIu32, value.get<std::uint32_t>());
        case ScalarType::S8:
            return out.append("%d", static_cast<int>(value.get<std::int8_t>()));
        case ScalarType::S16:
            return out.append("%d", static_cast<int>(value.get<std::int16_t>()));
        case ScalarType::S32:
            return out.append("%" PRId32, value.get<std::int32_t>());
        case ScalarType::U64:
            return out.appendInteger(value.get<std::uint64_t>());
        case ScalarType::S64:
            return out.appendInteger(value.get<std::int64_t>());
        case ScalarType::Bool:
            return out.append(value.get<bool>() ? "true" : "false");
        default:
            return out.append("null");
    }
}

struct EnumJsonContext {
    Writer& out;
    bool first = true;
};

bool append_enum_entry_(void* context, const Scalar& value, std::string_view name) noexcept
{
    auto& state = *static_cast<EnumJsonContext*>(context);
    if (!state.first && !state.out.append(",")) return false;
    state.first = false;
    if (!state.out.append("\"")) return false;
    // Enum descriptions only produce supported integral alternatives. The
    // only one that may not fit int64_t is the upper half of uint64_t.
    if (const auto signedCode = convertScalar<std::int64_t>(value)) {
        if (!state.out.appendInteger(*signedCode)) return false;
    } else if (!state.out.appendInteger(value.get<std::uint64_t>())) return false;
    return state.out.append("\":") && state.out.appendString(name);
}

std::uint32_t hash_u64_(std::uint32_t hash, std::uint64_t value) noexcept
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash = hash_byte_(hash, static_cast<std::uint8_t>(value >> shift));
    }
    return hash;
}

bool hash_enum_entry_(void* context, const Scalar& value, std::string_view name) noexcept
{
    auto& hash = *static_cast<std::uint32_t*>(context);
    const auto signedCode = convertScalar<std::int64_t>(value);
    const auto bits = signedCode ? static_cast<std::uint64_t>(*signedCode) : value.get<std::uint64_t>();
    hash = hash_byte_(hash, 'V');
    hash = hash_u64_(hash, bits);
    // Length also separates custom names containing embedded NUL characters.
    hash = hash_u64_(hash, static_cast<std::uint64_t>(name.size()));
    for (char byte : name) hash = hash_byte_(hash, static_cast<std::uint8_t>(byte));
    return true;
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

std::size_t writeSchema(const CatalogIndex& index, char* const buffer, const std::size_t size) noexcept
{
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    Writer out {buffer, size};
    if (!out.ok()) return 0;
    if (!out.append("{\"schema\":\"%08lx\",\"catalogs\":[",
                    static_cast<unsigned long>(schemaCrc(index)))) return 0;
    for (std::size_t c = 0u; c < count; ++c) {
        if (!out.append("%s{\"id\":%u,\"name\":\"%s\",\"fields\":[",
                        (c == 0u) ? "" : ",", static_cast<unsigned>(catalogs[c].id), catalogs[c].name)) return 0;
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            const Field& field = catalogs[c].fields[i];
            if (!out.append("%s{\"i\":%u,\"id\":%" PRIu32 ",\"n\":\"%s\",\"u\":\"%s\",\"t\":\"%s\",\"w\":%s",
                            (i == 0u) ? "" : ",", static_cast<unsigned>(i),
                            field.id, field.name, field.unit, type_name_(field.declaredType),
                            field.set ? "true" : "false")) return 0;
            if (field.declaredType.hasEnum()) {
                if (!out.append(",\"enum\":{")) return 0;
                EnumJsonContext context{out};
                if (!field.declaredType.describeEnum(&context, &append_enum_entry_)) return 0;
                if (!out.append("}")) return 0;
            }
            if (!out.append("}")) return 0;
        }
        if (!out.append("]}")) return 0;
    }
    (void) out.append("]}");
    return out.length();
}

std::size_t writeValues(const CatalogIndex& index, char* const buffer, const std::size_t size) noexcept
{
    const Catalog* const catalogs = index.data();
    const std::size_t count = index.size();
    Writer out {buffer, size};
    if (!out.append("{")) return 0;
    for (std::size_t c = 0u; c < count; ++c) {
        if (!out.append("%s\"%s\":[", (c == 0u) ? "" : ",", catalogs[c].name)) return 0;
        for (std::size_t i = 0u; i < catalogs[c].count; ++i) {
            if (i != 0u && !out.append(",")) return 0;
            if (!append_value_(out, catalogs[c].fields[i])) return 0;
        }
        if (!out.append("]")) return 0;
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
