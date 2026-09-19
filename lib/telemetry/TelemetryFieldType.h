/**
 * @file TelemetryFieldType.h
 * @brief Numeric field types, checked write limits, defaults and optional enum schema metadata.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 */
#ifndef TELEMETRY_FIELD_TYPE_H
#define TELEMETRY_FIELD_TYPE_H

#include "TelemetryConversion.h"
#include <cstdlib>
#include <limits>
#include <string_view>
#include <variant>

namespace telemetry {

// The value and name are borrowed for the duration of the sink call. Return false
// to stop enumeration immediately. No source getter or setter is involved.
using EnumEntrySink = bool (*)(void*, const Scalar&, std::string_view) noexcept;
using EnumDescription = bool (*)(void*, EnumEntrySink) noexcept;

namespace detail {
template <class T>
struct NumericLimits {
    T minimum = std::numeric_limits<T>::lowest();
    T maximum = std::numeric_limits<T>::max();
    T initial{};
};

// Invalid definitions fail constant evaluation in constexpr tables. The same
// programming error terminates deterministically when constructed at runtime.
[[noreturn]] inline void invalidFieldLimits() noexcept { std::abort(); }
} // namespace detail

class FieldType {
    using Limits = std::variant<std::monostate,
        detail::NumericLimits<float>, detail::NumericLimits<double>,
        detail::NumericLimits<std::uint32_t>, detail::NumericLimits<std::int32_t>,
        detail::NumericLimits<std::uint64_t>, detail::NumericLimits<bool>,
        detail::NumericLimits<std::uint8_t>, detail::NumericLimits<std::uint16_t>,
        detail::NumericLimits<std::int8_t>, detail::NumericLimits<std::int16_t>,
        detail::NumericLimits<std::int64_t>>;

    template <unsigned Which>
    constexpr Scalar project_() const noexcept
    {
        return std::visit([](const auto& limits) constexpr noexcept -> Scalar {
            if constexpr (std::is_same_v<std::decay_t<decltype(limits)>, std::monostate>) return {};
            else if constexpr (Which == 0) return Scalar::from(limits.minimum);
            else if constexpr (Which == 1) return Scalar::from(limits.maximum);
            else return Scalar::from(limits.initial);
        }, limits_);
    }

public:
    // Native extrema and zero/false default. Floating extrema are finite.
    // Implicit construction preserves rows containing ScalarType::F32, etc.
    constexpr FieldType(ScalarType type = ScalarType::Null) noexcept
        : valueType_(type), limits_(nativeLimits_(type)) {}

    TELEMETRY_FORCE_INLINE constexpr operator ScalarType() const noexcept { return valueType_; }
    constexpr bool hasEnum() const noexcept { return describe_ != nullptr; }

    // These projections belong to metadata consumers, not the read path.
    constexpr Scalar minimum() const noexcept { return project_<0>(); }
    constexpr Scalar maximum() const noexcept { return project_<1>(); }
    constexpr Scalar defaultValue() const noexcept { return project_<2>(); }

    // Definitions use the same checked numeric conversions as writes. All
    // three values must be finite, with minimum <= initial <= maximum.
    // Return a new descriptor; published metadata remains immutable.
    constexpr FieldType withLimits(Scalar minimum, Scalar maximum, Scalar initial) const noexcept
    {
        // Normalize local values before extracting the native limits. This
        // also keeps runtime construction free of intermediate optionals.
        if (!convertScalar(minimum, valueType_, minimum)
            || !convertScalar(maximum, valueType_, maximum)
            || !convertScalar(initial, valueType_, initial)) detail::invalidFieldLimits();
        FieldType result = *this;
        const bool valid = std::visit([&](auto& limits) constexpr noexcept {
            using L = std::decay_t<decltype(limits)>;
            if constexpr (std::is_same_v<L, std::monostate>) return false;
            else {
                using T = decltype(limits.minimum);
                const T low = minimum.get<T>();
                const T high = maximum.get<T>();
                const T start = initial.get<T>();
                if constexpr (std::is_floating_point_v<T>) {
                    if (!detail::scalarFinite(low) || !detail::scalarFinite(high)
                        || !detail::scalarFinite(start)) return false;
                }
                if (!(low <= start && start <= high)) return false;
                limits = {low, high, start};
                result.restricted_ = low != std::numeric_limits<T>::lowest()
                    || high != std::numeric_limits<T>::max();
                return true;
            }
        }, result.limits_);
        if (!valid) detail::invalidFieldLimits();
        return result;
    }

    constexpr FieldType withDefault(Scalar initial) const noexcept
    {
        return withLimits(minimum(), maximum(), initial);
    }

    // A numeric-only descriptor or an absent sink returns false. A context
    // may be null when the supplied sink does not need application state.
    bool describeEnum(void* context, EnumEntrySink sink) const noexcept
    {
        return describe_ != nullptr && sink != nullptr && describe_(context, sink);
    }

private:
    constexpr FieldType(ScalarType type, EnumDescription describe) noexcept
        : FieldType(type) { describe_ = describe; }

    static constexpr Limits nativeLimits_(ScalarType type) noexcept
    {
        switch (type) {
            case ScalarType::F32: return detail::NumericLimits<float>{};
            case ScalarType::F64: return detail::NumericLimits<double>{};
            case ScalarType::U8: return detail::NumericLimits<std::uint8_t>{};
            case ScalarType::U16: return detail::NumericLimits<std::uint16_t>{};
            case ScalarType::U32: return detail::NumericLimits<std::uint32_t>{};
            case ScalarType::U64: return detail::NumericLimits<std::uint64_t>{};
            case ScalarType::S8: return detail::NumericLimits<std::int8_t>{};
            case ScalarType::S16: return detail::NumericLimits<std::int16_t>{};
            case ScalarType::S32: return detail::NumericLimits<std::int32_t>{};
            case ScalarType::S64: return detail::NumericLimits<std::int64_t>{};
            case ScalarType::Bool: return detail::NumericLimits<bool>{};
            default: return std::monostate{};
        }
    }

    template <class T>
    TELEMETRY_FORCE_INLINE constexpr bool contains_(const Scalar& value) const noexcept
    {
        const T number = value.get<T>();
        const auto& limits = std::get<detail::NumericLimits<T>>(limits_);
        return number >= limits.minimum && number <= limits.maximum;
    }

    // Field calls this only after successful conversion to valueType_.
    // No enum metadata is consulted. Full-range integer writes need no
    // additional comparisons; float writes always reject NaN and infinity.
    TELEMETRY_FORCE_INLINE constexpr bool acceptsConverted_(const Scalar& value) const noexcept
    {
        if (!restricted_ && valueType_ != ScalarType::F32 && valueType_ != ScalarType::F64) return true;
        switch (valueType_) {
            case ScalarType::F32: return contains_<float>(value);
            case ScalarType::F64: return contains_<double>(value);
            case ScalarType::U8: return contains_<std::uint8_t>(value);
            case ScalarType::U16: return contains_<std::uint16_t>(value);
            case ScalarType::U32: return contains_<std::uint32_t>(value);
            case ScalarType::U64: return contains_<std::uint64_t>(value);
            case ScalarType::S8: return contains_<std::int8_t>(value);
            case ScalarType::S16: return contains_<std::int16_t>(value);
            case ScalarType::S32: return contains_<std::int32_t>(value);
            case ScalarType::S64: return contains_<std::int64_t>(value);
            case ScalarType::Bool: return contains_<bool>(value);
            default: return false;
        }
    }

    template <class E, E... Values>
    friend constexpr FieldType enumType() noexcept;
    friend struct Field;

    ScalarType valueType_ = ScalarType::Null;
    bool restricted_ = false;
    EnumDescription describe_ = nullptr;
    Limits limits_{};
};

static_assert(std::is_trivially_copyable_v<FieldType>, "Field type metadata must remain trivial");

template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
constexpr FieldType numericType() noexcept
{
    return Scalar::from(T{}).type();
}

// Default first; omitted bounds retain the full native range. Keep Scalar
// parameters so conversions are checked before any narrowing to T occurs.
template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
constexpr FieldType numericType(Scalar initial,
    Scalar minimum = Scalar::from(std::numeric_limits<T>::lowest()),
    Scalar maximum = Scalar::from(std::numeric_limits<T>::max())) noexcept
{
    return numericType<T>().withLimits(minimum, maximum, initial);
}

} // namespace telemetry

#endif
