/**
 * @file TelemetryLimits.h
 * @brief Optional default and bounds for signature-inferred definitions.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_LIMITS_H
#define TELEMETRY_LIMITS_H

#include "../detail/TelemetryCallable.h"

namespace telemetry {
namespace detail {
struct NoLimits {};
template <class T, bool Bounded> struct ValueLimits {
    T initial;
    T minimum;
    T maximum;
};
template <class T> struct ValueLimits<T, false> { T initial; };
template <class E, E... Values> struct EnumSpec {
    E initial;
};
template <class T> struct IsLimits : std::false_type {};
template <> struct IsLimits<NoLimits> : std::true_type {};
template <class T, bool B> struct IsLimits<ValueLimits<T, B>> : std::true_type {};
template <class E, E... Values> struct IsLimits<EnumSpec<E, Values...>> : std::true_type {};

template <class T>
constexpr FieldType refineType(NoLimits) noexcept { return inferredType<T>(); }

template <class T, class U, bool Bounded>
constexpr FieldType refineType(ValueLimits<U, Bounded> values) noexcept
{
    static_assert(std::is_same_v<T, U>, "Metadata values must match the signature's exact C++ type");
    constexpr auto base = inferredType<T>();
    if constexpr (Bounded) {
        // Enum refinements cannot widen the inferred interval. In particular,
        // converting out-of-range integers to an unfixed enum must never occur.
        if constexpr (std::is_enum_v<T>) {
            using Stored = Scalar::NativeType<Scalar::from(RawNumberT<T>{}).type()>;
            if (static_cast<RawNumberT<T>>(values.minimum) < base.minimum().template get<Stored>()
                || static_cast<RawNumberT<T>>(values.maximum) > base.maximum().template get<Stored>())
                invalidFieldLimits();
        }
        return base.withLimits(factoryScalar(values.minimum), factoryScalar(values.maximum),
                               factoryScalar(values.initial));
    } else return base.withDefault(factoryScalar(values.initial));
}

template <class T, class E, E... Values>
constexpr FieldType refineType(EnumSpec<E, Values...> values) noexcept
{
    static_assert(std::is_same_v<T, E>,
                  "enumSpec values must match the signature's exact enum type");
    static_assert(sizeof...(Values) != 0, "enumSpec requires at least one enumerator");
    return enumType<E, Values...>(values.initial);
}

template <class T>
constexpr FieldType enumConstraintType(NoLimits) noexcept { return inferredType<T>(); }
template <class T, class U, bool Bounded>
constexpr FieldType enumConstraintType(ValueLimits<U, Bounded>) noexcept { return inferredType<T>(); }
template <class T, class E, E... Values>
constexpr FieldType enumConstraintType(EnumSpec<E, Values...>) noexcept
{
    static_assert(std::is_same_v<T, E>,
                  "enumSpec values must match the signature's exact enum type");
    return enumType<E, Values...>();
}
} // namespace detail

template <class T>
constexpr auto limits(T initial, T minimum, T maximum) noexcept
{
    static_assert(detail::isFactoryValue<T>, "limits requires numeric or enum values");
    return detail::ValueLimits<T, true>{initial, minimum, maximum};
}

template <class T>
constexpr auto limits(T initial) noexcept
{
    static_assert(detail::isFactoryValue<T>, "limits requires a numeric or enum value");
    return detail::ValueLimits<T, false>{initial};
}


// Explicit enum dictionaries cover sparse, large or intentionally filtered
// code sets outside magic_enum's automatic scan. With no explicit initial
// value, the smallest listed numeric code is the default.
template <auto First, auto... Rest>
constexpr auto enumSpec() noexcept
{
    using E = decltype(First);
    static_assert(std::is_enum_v<E>, "enumSpec requires enum values");
    static_assert((std::is_same_v<E, decltype(Rest)> && ...),
                  "All enumSpec values must have the exact same enum type");
    constexpr auto type = enumType<E, First, Rest...>();
    using Raw = std::underlying_type_t<E>;
    constexpr auto tag = Scalar::from(Raw{}).type();
    using Stored = Scalar::NativeType<tag>;
    return detail::EnumSpec<E, First, Rest...>{
        static_cast<E>(static_cast<Raw>(type.defaultValue().template get<Stored>()))};
}

template <auto First, auto... Rest>
constexpr auto enumSpec(decltype(First) initial) noexcept
{
    using E = decltype(First);
    static_assert(std::is_enum_v<E>, "enumSpec requires enum values");
    static_assert((std::is_same_v<E, decltype(Rest)> && ...),
                  "All enumSpec values must have the exact same enum type");
    return detail::EnumSpec<E, First, Rest...>{initial};
}
} // namespace telemetry
#endif
