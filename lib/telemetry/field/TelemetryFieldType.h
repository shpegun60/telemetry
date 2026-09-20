/**
 * @file TelemetryFieldType.h
 * @brief typed write bounds separated from default/enum metadata.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_TYPE_H
#define TELEMETRY_FIELD_TYPE_H

#include "../core/TelemetryConversion.h"
#include "../detail/TelemetryBounds.h"
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace telemetry {
using EnumEntrySink = bool (*)(void*, const Scalar&, std::string_view) noexcept;
using EnumDescription = bool (*)(void*, EnumEntrySink) noexcept;

namespace detail {
struct FieldTableAccess;
[[noreturn]] inline void invalidFieldLimits() noexcept { std::abort(); }
} // namespace detail

class FieldType {
    template <class T>
    TELEMETRY_FORCE_INLINE constexpr const detail::NumericBounds<T>& boundsFor_() const noexcept
    {
        if constexpr (std::is_same_v<T, float>) return bounds_.f32;
        else if constexpr (std::is_same_v<T, double>) return bounds_.f64;
        else if constexpr (std::is_same_v<T, std::uint32_t>) return bounds_.u32;
        else if constexpr (std::is_same_v<T, std::int32_t>) return bounds_.s32;
        else if constexpr (std::is_same_v<T, std::uint64_t>) return bounds_.u64;
        else if constexpr (std::is_same_v<T, bool>) return bounds_.boolean;
        else if constexpr (std::is_same_v<T, std::uint8_t>) return bounds_.u8;
        else if constexpr (std::is_same_v<T, std::uint16_t>) return bounds_.u16;
        else if constexpr (std::is_same_v<T, std::int8_t>) return bounds_.s8;
        else if constexpr (std::is_same_v<T, std::int16_t>) return bounds_.s16;
        else if constexpr (std::is_same_v<T, std::int64_t>) return bounds_.s64;
    }

    template <class T, unsigned Which>
    constexpr Scalar projectAs_() const noexcept
    {
        if constexpr (Which == 0) return Scalar::from(boundsFor_<T>().minimum);
        else return Scalar::from(boundsFor_<T>().maximum);
    }

    template <unsigned Which>
    constexpr Scalar project_() const noexcept
    {
        switch (valueType_) {
            case ScalarType::F32: return projectAs_<float, Which>();
            case ScalarType::F64: return projectAs_<double, Which>();
            case ScalarType::U32: return projectAs_<std::uint32_t, Which>();
            case ScalarType::S32: return projectAs_<std::int32_t, Which>();
            case ScalarType::U64: return projectAs_<std::uint64_t, Which>();
            case ScalarType::Bool: return projectAs_<bool, Which>();
            case ScalarType::U8: return projectAs_<std::uint8_t, Which>();
            case ScalarType::U16: return projectAs_<std::uint16_t, Which>();
            case ScalarType::S8: return projectAs_<std::int8_t, Which>();
            case ScalarType::S16: return projectAs_<std::int16_t, Which>();
            case ScalarType::S64: return projectAs_<std::int64_t, Which>();
            default: return {};
        }
    }

    template <class T>
    constexpr FieldType(detail::NumericBounds<T> bounds, T initial, EnumDescription describe) noexcept
        : valueType_(Scalar::from(T{}).type()),
          restricted_(bounds.minimum != std::numeric_limits<T>::lowest()
              || bounds.maximum != std::numeric_limits<T>::max()),
          bounds_(bounds), initial_(Scalar::from(initial)), describe_(describe) {}

    template <class T>
    constexpr FieldType checked_(const Scalar& minimum, const Scalar& maximum, const Scalar& initial) const noexcept
    {
        const T low = minimum.get<T>(), high = maximum.get<T>(), start = initial.get<T>();
        if constexpr (std::is_floating_point_v<T>) {
            if (!detail::scalarFinite(low) || !detail::scalarFinite(high)
                || !detail::scalarFinite(start)) detail::invalidFieldLimits();
        }
        if (!(low <= start && start <= high)) detail::invalidFieldLimits();
        return FieldType(detail::NumericBounds<T>{low, high}, start, describe_);
    }


public:
    constexpr FieldType(ScalarType type = ScalarType::Null) noexcept
        : valueType_(type), bounds_(nativeBounds_(type)), initial_(nativeDefault_(type)) {}

    TELEMETRY_FORCE_INLINE constexpr operator ScalarType() const noexcept { return valueType_; }
    constexpr bool hasEnum() const noexcept { return describe_ != nullptr; }
    constexpr Scalar minimum() const noexcept { return project_<0>(); }
    constexpr Scalar maximum() const noexcept { return project_<1>(); }
    constexpr Scalar defaultValue() const noexcept { return initial_; }

    constexpr FieldType withLimits(Scalar minimum, Scalar maximum, Scalar initial) const noexcept
    {
        if (!convertScalar(minimum, valueType_, minimum)
            || !convertScalar(maximum, valueType_, maximum)
            || !convertScalar(initial, valueType_, initial)) detail::invalidFieldLimits();
        switch (valueType_) {
            case ScalarType::F32: return checked_<float>(minimum, maximum, initial);
            case ScalarType::F64: return checked_<double>(minimum, maximum, initial);
            case ScalarType::U32: return checked_<std::uint32_t>(minimum, maximum, initial);
            case ScalarType::S32: return checked_<std::int32_t>(minimum, maximum, initial);
            case ScalarType::U64: return checked_<std::uint64_t>(minimum, maximum, initial);
            case ScalarType::Bool: return checked_<bool>(minimum, maximum, initial);
            case ScalarType::U8: return checked_<std::uint8_t>(minimum, maximum, initial);
            case ScalarType::U16: return checked_<std::uint16_t>(minimum, maximum, initial);
            case ScalarType::S8: return checked_<std::int8_t>(minimum, maximum, initial);
            case ScalarType::S16: return checked_<std::int16_t>(minimum, maximum, initial);
            case ScalarType::S64: return checked_<std::int64_t>(minimum, maximum, initial);
            default: detail::invalidFieldLimits();
        }
    }

    constexpr FieldType withDefault(Scalar initial) const noexcept
    {
        return withLimits(minimum(), maximum(), initial);
    }

    bool describeEnum(void* context, EnumEntrySink sink) const noexcept
    {
        return describe_ != nullptr && sink != nullptr && describe_(context, sink);
    }

    // Exact private layout for the link-time ABI signature.
    static constexpr std::size_t abiValueTypeOffset() noexcept
    { return offsetof(FieldType, valueType_); }
    static constexpr std::size_t abiRestrictedOffset() noexcept
    { return offsetof(FieldType, restricted_); }
    static constexpr std::size_t abiBoundsOffset() noexcept
    { return offsetof(FieldType, bounds_); }
    static constexpr std::size_t abiInitialOffset() noexcept
    { return offsetof(FieldType, initial_); }
    static constexpr std::size_t abiDescribeOffset() noexcept
    { return offsetof(FieldType, describe_); }
    static constexpr std::size_t abiBoundsSize() noexcept
    { return sizeof(detail::FieldBounds); }
    static constexpr std::size_t abiBoundsAlign() noexcept
    { return alignof(detail::FieldBounds); }

private:
    constexpr FieldType(ScalarType type, EnumDescription describe) noexcept
        : FieldType(type) { describe_ = describe; }

    static constexpr detail::FieldBounds nativeBounds_(ScalarType type) noexcept
    {
        switch (type) {
            case ScalarType::F32: return detail::FieldBounds(detail::NumericBounds<float>{});
            case ScalarType::F64: return detail::FieldBounds(detail::NumericBounds<double>{});
            case ScalarType::U32: return detail::FieldBounds(detail::NumericBounds<std::uint32_t>{});
            case ScalarType::S32: return detail::FieldBounds(detail::NumericBounds<std::int32_t>{});
            case ScalarType::U64: return detail::FieldBounds(detail::NumericBounds<std::uint64_t>{});
            case ScalarType::Bool: return detail::FieldBounds(detail::NumericBounds<bool>{});
            case ScalarType::U8: return detail::FieldBounds(detail::NumericBounds<std::uint8_t>{});
            case ScalarType::U16: return detail::FieldBounds(detail::NumericBounds<std::uint16_t>{});
            case ScalarType::S8: return detail::FieldBounds(detail::NumericBounds<std::int8_t>{});
            case ScalarType::S16: return detail::FieldBounds(detail::NumericBounds<std::int16_t>{});
            case ScalarType::S64: return detail::FieldBounds(detail::NumericBounds<std::int64_t>{});
            default: return {};
        }
    }

    static constexpr Scalar nativeDefault_(ScalarType type) noexcept
    {
        switch (type) {
            case ScalarType::F32: return Scalar::from(float{});
            case ScalarType::F64: return Scalar::from(double{});
            case ScalarType::U32: return Scalar::from(std::uint32_t{});
            case ScalarType::S32: return Scalar::from(std::int32_t{});
            case ScalarType::U64: return Scalar::from(std::uint64_t{});
            case ScalarType::Bool: return Scalar::from(bool{});
            case ScalarType::U8: return Scalar::from(std::uint8_t{});
            case ScalarType::U16: return Scalar::from(std::uint16_t{});
            case ScalarType::S8: return Scalar::from(std::int8_t{});
            case ScalarType::S16: return Scalar::from(std::int16_t{});
            case ScalarType::S64: return Scalar::from(std::int64_t{});
            default: return {};
        }
    }

    template <class T>
    TELEMETRY_FORCE_INLINE constexpr bool containsNative_(T number) const noexcept
    {
        const auto& bounds = boundsFor_<T>();
        return number >= bounds.minimum && number <= bounds.maximum;
    }

    // T is the already-normalized Scalar alternative. Only the generated
    // table access and Field may call this; the bounds union uses that type.
    template <class T>
    TELEMETRY_FORCE_INLINE constexpr bool acceptsNative_(T number) const noexcept
    {
        if (!restricted_) {
            if constexpr (std::is_floating_point_v<T>) return detail::scalarFinite(number);
            else return true;
        }
        return containsNative_(number);
    }

    TELEMETRY_FORCE_INLINE constexpr bool acceptsConverted_(const Scalar& value) const noexcept
    {
        if (!restricted_) {
            if (valueType_ == ScalarType::F32) return detail::scalarFinite(value.get<float>());
            if (valueType_ == ScalarType::F64) return detail::scalarFinite(value.get<double>());
            return true;
        }
        switch (valueType_) {
            case ScalarType::F32: return acceptsNative_(value.get<float>());
            case ScalarType::F64: return acceptsNative_(value.get<double>());
            case ScalarType::U32: return acceptsNative_(value.get<std::uint32_t>());
            case ScalarType::S32: return acceptsNative_(value.get<std::int32_t>());
            case ScalarType::U64: return acceptsNative_(value.get<std::uint64_t>());
            case ScalarType::Bool: return acceptsNative_(value.get<bool>());
            case ScalarType::U8: return acceptsNative_(value.get<std::uint8_t>());
            case ScalarType::U16: return acceptsNative_(value.get<std::uint16_t>());
            case ScalarType::S8: return acceptsNative_(value.get<std::int8_t>());
            case ScalarType::S16: return acceptsNative_(value.get<std::int16_t>());
            case ScalarType::S64: return acceptsNative_(value.get<std::int64_t>());
            default: return false;
        }
    }

    template <class E, E... Values>
    friend constexpr FieldType enumType() noexcept;
    friend struct Field;
    friend struct detail::FieldTableAccess;

    ScalarType valueType_ = ScalarType::Null;
    bool restricted_ = false;
    detail::FieldBounds bounds_{};
    Scalar initial_{};
    EnumDescription describe_ = nullptr;

    static constexpr std::size_t writeBytes_() noexcept { return offsetof(FieldType, initial_); }
};
static_assert(std::is_trivially_copyable_v<FieldType>);

template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
constexpr FieldType numericType() noexcept
{
    return Scalar::from(T{}).type();
}

template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
constexpr FieldType numericType(Scalar initial,
    Scalar minimum = Scalar::from(std::numeric_limits<T>::lowest()),
    Scalar maximum = Scalar::from(std::numeric_limits<T>::max())) noexcept
{
    return numericType<T>().withLimits(minimum, maximum, initial);
}
} // namespace telemetry

#endif
