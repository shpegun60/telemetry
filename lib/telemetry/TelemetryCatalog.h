/**
 * @file TelemetryCatalog.h
 * @brief Packed field IDs, read/write field contracts and borrowed catalogs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 */
#ifndef TELEMETRY_CATALOG_H
#define TELEMETRY_CATALOG_H

#include <cstddef>
#include <cstdint>

#include "TelemetryGetter.h"
#include "TelemetryCompiler.h"
#include "TelemetrySetter.h"
#include "TelemetryConversion.h"
#include "TelemetryFieldType.h"
#include "TelemetryCacheline.h"

namespace telemetry {

// In-memory ABI revision, not a wire-format version or a link-time guard.
// Revision 3 separates read/write contracts and makes definitions immutable.
inline constexpr std::uint32_t telemetryAbiVersion = 3;

// Packed identity: high 16 bits are the zero-based group position; low
// 16 bits are the zero-based field position within that group.
using FieldId = std::uint32_t;
using GroupId = std::uint16_t;
using FieldOffset = std::uint16_t;
inline constexpr std::uint32_t idComponentCapacity = 65536u;

constexpr FieldId makeId(GroupId group, FieldOffset index) noexcept
{
    return (FieldId{group} << 16) | FieldId{index};
}

constexpr GroupId groupOf(FieldId id) noexcept
{
    return static_cast<GroupId>(id >> 16);
}

constexpr FieldOffset indexOf(FieldId id) noexcept
{
    return static_cast<FieldOffset>(id & 0xffffu);
}

// Catalogs describe values; bound owners acquire and apply their values.
// Getter/Setter apply telemetry's noexcept/empty-value policy to tiny::delegate_ref.
// This layer adds no synchronization or cross-field snapshot guarantee.
// A getter must be noexcept and return null when its value is unavailable.
// All referenced objects, arrays and strings must outlive their consumers.
// Field metadata and addresses stay unchanged from Catalog construction;
// only values inside the bound source objects may change during use.
// ARM32: Getter/readType occupy line 0; Setter, numeric tag and bounds line 1.
// The default Scalar and enum description are cold; the Field stride stays 96.
// Positional table initialization is preserved by the constexpr constructor.
// RW32: independent read and write prefixes in one array element.
struct alignas(cacheLineBytes) Field {
    const Getter get;
    const ScalarType readType;
    const FieldId id;
    const char* const name;
    const char* const unit;

    alignas(cacheLineBytes) const Setter set;
    const FieldType declaredType;

    static_assert(sizeof(Getter) + sizeof(ScalarType) <= cacheLineBytes,
                  "Cache line must contain the getter and read type");
    static_assert(sizeof(Setter) % alignof(FieldType) == 0
                  && sizeof(Setter) + FieldType::writeBytes_() <= cacheLineBytes,
                  "Cache line must contain the complete setter/type/bounds contract");

    constexpr Field(FieldId fieldId = 0, const char* fieldName = "",
                    const char* fieldUnit = "", FieldType fieldType = ScalarType::Null,
                    Getter getter = nullptr, Setter setter = nullptr) noexcept
        : get(getter), readType(static_cast<ScalarType>(fieldType)), id(fieldId),
          name(fieldName), unit(fieldUnit), set(setter), declaredType(fieldType) {}

    constexpr Field(const Field&) noexcept = default;
    constexpr Field(Field&&) noexcept = default;
    // Const members keep the duplicated read tag and complete definition in sync.
    // Assignment is implicitly deleted; copy/move construction remains trivial.

    // readType is the value contract for both reads and writes. Invoke the
    // getter once, then normalize in place; a failed conversion yields Null.
    // Matching numeric types preserve their payload without a numeric cast.
    [[nodiscard]] TELEMETRY_FORCE_INLINE Scalar read() const noexcept
    {
        Scalar value = get();
        return convertScalar(value, readType, value) ? value : Scalar::null();
    }

    // Normalize to readType before adapting to the requested C++ type.
    // This must preserve the declared type's rounding, truncation and range
    // even when T happens to equal the getter's original result type.
    template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE std::optional<T> read() const noexcept
    {
        Scalar value = get();
        constexpr auto requestedType = Scalar::from(T{}).type();
        // When the requested C++ type is exactly the declared alternative,
        // conversion already enforces the field's complete value contract.
        // Return it directly without constructing another Scalar in between.
        if constexpr (std::is_same_v<T, Scalar::NativeType<requestedType>>) {
            if (readType == requestedType) return convertScalar<T>(value);
        }
        std::optional<T> result;
        if (convertScalar(value, readType, value)) result = convertScalar<T>(value);
        return result;
    }

    // Presence is checked before conversion, so a read-only field always reports
    // ReadOnly. Conversion/range failure never calls the setter or reads the getter.
    template <class T, std::enable_if_t<detail::isScalarNumber<T> || std::is_same_v<T, Scalar>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    WriteResult write(T value) const noexcept
    {
        if (!set) return WriteResult::ReadOnly;
        // Start with the actual value and normalize in place, as in read().
        // Native source tags are known at compilation. There is no initial
        // empty Scalar to clear before storing a successful write value.
        Scalar converted = Scalar::from(value);
        if (!convertScalar(converted, declaredType, converted)) return WriteResult::InvalidValue;
        if (!declaredType.acceptsConverted_(converted)) return WriteResult::InvalidValue;
        return set(converted);
    }
};


struct Catalog {
    const GroupId id = 0;
    const char* const name = "";
    const Field* const fields = nullptr;
    const std::size_t count = 0;

    constexpr Catalog() noexcept = default;

    // Validate once, retaining only the correct contiguous prefix. Counts
    // above 65536 are clipped before indexing or narrowing the row ordinal.
    // The pointer/count must describe a live array; null produces no fields.
    // A template keeps the deleted rvalue-array overload preferred to
    // array-to-pointer conversion in the explicit pointer/count form.
    template <class = void>
    constexpr Catalog(GroupId group, const char* label, const Field* rows,
                      std::size_t requestedCount) noexcept
        : id(group), name(label), fields(rows), count(prefixCount_(group, rows, requestedCount)) {}

    template <std::size_t N>
    constexpr Catalog(GroupId group, const char* label, const Field (&rows)[N]) noexcept
        : Catalog(group, label, rows, N) {}

    template <std::size_t N>
    Catalog(GroupId, const char*, const Field (&&)[N]) = delete;
    template <std::size_t N>
    Catalog(GroupId, const char*, const Field (&&)[N], std::size_t) = delete;

private:
    static constexpr std::size_t prefixCount_(GroupId group, const Field* rows,
                                             std::size_t requestedCount) noexcept
    {
        if (rows == nullptr) return 0;
        const std::size_t limit = requestedCount < idComponentCapacity
            ? requestedCount : idComponentCapacity;
        std::size_t accepted = 0;
        while (accepted < limit && rows[accepted].id == makeId(group, static_cast<FieldOffset>(accepted))) {
            ++accepted;
        }
        return accepted;
    }
};

constexpr bool str_equal(const char* a, const char* b) noexcept
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

// Optional definition-time validation, never part of lookup/read/write.
// The pointer/count must describe an actual array; nullptr is valid only empty.
constexpr bool names_unique(const Field* fields, std::size_t count) noexcept
{
    if (fields == nullptr) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (fields[i].name == nullptr) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (str_equal(fields[i].name, fields[j].name)) {
                return false;
            }
        }
    }
    return true;
}

// Optional definition-time validation, never part of lookup/read/write.
// The pointer/count must describe an actual array; nullptr is valid only empty.
constexpr bool catalog_names_unique(const Catalog* catalogs, std::size_t count) noexcept
{
    if (catalogs == nullptr) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (catalogs[i].name == nullptr) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (str_equal(catalogs[i].name, catalogs[j].name)) return false;
        }
    }
    return true;
}

} // namespace telemetry

#endif
