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
// Specifications own only their provided values. They carry no pointers into
// temporary storage and do not mutate the application object's initial state.
struct NoLimits {};
template <class T, bool Bounded> struct ValueLimits {
    T initial;
    T minimum;
    T maximum;
};
template <class T> struct ValueLimits<T, false> { T initial; };
template <class T> struct IsLimits : std::false_type {};
template <> struct IsLimits<NoLimits> : std::true_type {};
template <class T, bool B> struct IsLimits<ValueLimits<T, B>> : std::true_type {};
template <class E, E... Values> struct IsLimits<EnumSpec<E, Values...>> : std::true_type {};
template <class T> struct IsBoundedLimits : std::false_type {};
template <class T> struct IsBoundedLimits<ValueLimits<T, true>> : std::true_type {};

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
} // namespace detail

template <class T>
constexpr auto limits(T initial, T minimum, T maximum) noexcept
{
    // One deduced T keeps limits tied to the exact callback signature; no
    // implicit mixed-type metadata conversion is hidden in this factory.
    static_assert(detail::isFactoryValue<T>, "limits requires numeric or enum values");
    return detail::ValueLimits<T, true>{initial, minimum, maximum};
}

template <class T>
constexpr auto limits(T initial) noexcept
{
    static_assert(detail::isFactoryValue<T>, "limits requires a numeric or enum value");
    return detail::ValueLimits<T, false>{initial};
}
} // namespace telemetry
#endif
