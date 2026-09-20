/**
 * @file TelemetryEnum.h
 * @brief Compile-time enum names streamed to schema consumers as ordinary numeric values.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_ENUM_H
#define TELEMETRY_ENUM_H

#include "TelemetryFieldType.h"
#include "../../magic_enum/magic_enum.hpp"
#include <array>
#include <type_traits>
#include <utility>

namespace telemetry {
namespace detail {

template <class E, E... Values> struct EnumSpec {
    E initial;
};

template <class E, E... Values>
constexpr bool uniqueEnumCodes() noexcept
{
    constexpr std::array<E, sizeof...(Values)> values{Values...};
    for (std::size_t i = 1; i < values.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (values[i] == values[j]) return false;
        }
    }
    return true;
}

template <class E, E Value>
bool emitEnumEntry(void* context, EnumEntrySink sink) noexcept
{
    using Raw = std::underlying_type_t<E>;
    constexpr auto name = magic_enum::enum_name<Value>();
    // The name refers to static reflection storage; the numeric Scalar is
    // local to this callback. Sinks retaining a value must copy it.
    const Scalar number = Scalar::from(static_cast<Raw>(Value));
    return sink(context, number, name);
}

template <class E, E... Values>
bool describeEnum(void* context, EnumEntrySink sink) noexcept
{
    return (emitEnumEntry<E, Values>(context, sink) && ...);
}

template <class E, std::size_t... I>
constexpr EnumDescription automaticEnumDescription(std::index_sequence<I...>) noexcept
{
    // Only compile-time values become template arguments. There is no owned
    // EnumEntry array and no runtime reflection or enum-to-string search.
    constexpr auto values = magic_enum::enum_values<E>();
    return &describeEnum<E, values[I]...>;
}

template <class E, std::size_t N>
constexpr auto enumLimits(const std::array<E, N>& values) noexcept
{
    static_assert(N != 0, "Enum limits require at least one named code");
    using Raw = std::underlying_type_t<E>;
    Raw low = static_cast<Raw>(values[0]);
    Raw high = low;
    for (const E value : values) {
        const Raw number = static_cast<Raw>(value);
        if (number < low) low = number;
        if (number > high) high = number;
    }
    return NumericBounds<Raw>{low, high};
}

// Typed hot paths need only the raw numeric interval. Keep that interval
// independent of FieldType/Scalar so typed dispatch compiles to comparisons.
// Explicit dictionaries must use their own extrema, including named values
// outside the automatic scan. The interval permits unnamed interior values.
template <class E, class Constraint>
struct EnumConstraintBounds {
    static constexpr auto get() noexcept
    {
        constexpr auto values = magic_enum::enum_values<E>();
        static_assert(values.size() != 0,
                      "Enum dictionary is empty; configure the range or list values");
        return enumLimits(values);
    }
};

template <class E, class Listed, Listed... Values>
struct EnumConstraintBounds<E, EnumSpec<Listed, Values...>> {
    static_assert(std::is_same_v<E, Listed>,
                  "enumSpec values must match the signature's exact enum type");
    static constexpr auto get() noexcept
    {
        return enumLimits(std::array<Listed, sizeof...(Values)>{Values...});
    }
};

template <class E, class Constraint>
constexpr auto enumConstraintBounds() noexcept
{
    return EnumConstraintBounds<E, Constraint>::get();
}

} // namespace detail

// With no explicit values, magic_enum discovers the enumerators in its
// configured scan range (upstream default -128..127). For sparse or large
// codes use enumType<E, E::First, E::Second>(); names still come from E.
// Named extrema define the numeric write interval; its initial default is
// the smallest named code. Gaps remain writable; no membership check occurs.
template <class E, E... Values>
constexpr FieldType enumType() noexcept
{
    static_assert(std::is_enum_v<E>, "enumType requires an enum type");
    if constexpr (std::is_enum_v<E>) {
        using Raw = std::underlying_type_t<E>;
        static_assert(detail::isScalarNumber<Raw>, "Enum underlying type must be supported by Scalar");
        constexpr auto type = Scalar::from(Raw{}).type();
        if constexpr (sizeof...(Values) != 0) {
            static_assert(detail::uniqueEnumCodes<E, Values...>(), "Enum dictionary codes must be unique");
            static_assert(((!magic_enum::enum_name<Values>().empty()) && ...),
                          "Enum dictionary values must have names");
            constexpr auto limits = detail::enumLimits(std::array<E, sizeof...(Values)>{Values...});
            return FieldType{type, &detail::describeEnum<E, Values...>}
                .withLimits(limits.minimum, limits.maximum, limits.minimum);
        } else {
            constexpr auto values = magic_enum::enum_values<E>();
            static_assert(values.size() != 0, "Enum dictionary is empty; configure the range or list values");
            if constexpr (values.size() != 0) {
                constexpr auto limits = detail::enumLimits(values);
                return FieldType{type, detail::automaticEnumDescription<E>(std::make_index_sequence<values.size()>{})}
                    .withLimits(limits.minimum, limits.maximum, limits.minimum);
            } else return {};
        }
    } else {
        return {};
    }
}

// The caller can choose an initial numeric value within the automatic limits,
// including an unnamed code between the named extrema.
template <class E, E... Values>
constexpr FieldType enumType(E initial) noexcept
{
    static_assert(std::is_enum_v<E>, "enumType requires an enum type");
    if constexpr (std::is_enum_v<E>) {
        return enumType<E, Values...>().withDefault(static_cast<std::underlying_type_t<E>>(initial));
    } else return {};
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
    // Signature matching, named-code validation and default bounds are checked
    // when this lightweight specification is materialized into a descriptor.
    return detail::EnumSpec<E, First, Rest...>{initial};
}

} // namespace telemetry

#endif
