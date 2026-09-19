/**
 * @file TelemetryField.h
 * @brief Immutable telemetry field definition and read/write contract.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_H
#define TELEMETRY_FIELD_H

#include <optional>
#include <type_traits>

#include "../core/TelemetryCacheline.h"
#include "../core/TelemetryCompiler.h"
#include "../core/TelemetryConversion.h"
#include "TelemetryFieldType.h"
#include "TelemetryGetter.h"
#include "TelemetryId.h"
#include "TelemetrySetter.h"

namespace telemetry {

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

} // namespace telemetry

#endif
