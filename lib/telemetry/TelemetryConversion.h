/**
 * @file TelemetryConversion.h
 * @brief Checked numeric conversions shared by field reads and writes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 */
#ifndef TELEMETRY_CONVERSION_H
#define TELEMETRY_CONVERSION_H

#include "TelemetryScalar.h"
#include "TelemetryCompiler.h"
#include <limits>
#include <optional>

// Range/finite checks must remain effective before floating-to-integer casts.
#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ > 0)
#error "Compile telemetry conversions without fast-math or finite-math-only"
#endif

namespace telemetry {
namespace detail {

static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "Telemetry conversions require IEEE floating-point types");
static_assert(std::numeric_limits<float>::radix == 2 && std::numeric_limits<double>::radix == 2,
              "Telemetry conversion bounds require binary floating-point types");
static_assert(std::numeric_limits<float>::max_exponent > 64,
              "Every supported integer must fit in the floating-point range");

template <class Number>
constexpr bool scalarFinite(Number number) noexcept
{
    constexpr Number limit = std::numeric_limits<Number>::max();
    return number >= -limit && number <= limit;
}

template <class To, class From>
TELEMETRY_FORCE_INLINE constexpr bool convertNumberTo(From number, To& result) noexcept
{
    To converted{};
    if constexpr (std::is_same_v<To, From>) {
        result = number;
        return true;
    } else if constexpr (std::is_same_v<To, bool>) {
        if constexpr (std::is_floating_point_v<From>) {
            if (!scalarFinite(number)) return false;
        }
        converted = number != 0;
    } else if constexpr (std::is_floating_point_v<To>) {
        if constexpr (std::is_floating_point_v<From>) {
            if constexpr (std::numeric_limits<To>::max_exponent >= std::numeric_limits<From>::max_exponent) {
                converted = static_cast<To>(number);
            } else {
                // Common finite inputs need only the destination's two bounds.
                // Non-finite values are handled without an out-of-range cast.
                constexpr From limit = static_cast<From>(std::numeric_limits<To>::max());
                if (number >= -limit && number <= limit) converted = static_cast<To>(number);
                else if (number != number) converted = std::numeric_limits<To>::quiet_NaN();
                else if (number == std::numeric_limits<From>::infinity()) converted = std::numeric_limits<To>::infinity();
                else if (number == -std::numeric_limits<From>::infinity()) converted = -std::numeric_limits<To>::infinity();
                else return false;
            }
        } else {
            // Every int64/uint64 fits F32/F64's finite range. Precision may round.
            converted = static_cast<To>(number);
        }
    } else if constexpr (std::is_floating_point_v<From>) {
        static_assert(!std::is_signed_v<To>
                      || std::numeric_limits<To>::lowest() == -std::numeric_limits<To>::max() - 1,
                      "Floating-to-integer conversion requires a full two's-complement range");
        // Powers of two are exact in the source float type, including 2^64.
        // Never round max() into an inclusive upper bound. The lower bound
        // permits fractions truncating to min() only when the source precision
        // can represent them; otherwise min() itself is the first valid value.
        constexpr From upper = static_cast<From>(std::numeric_limits<To>::max() / 2 + 1) * From{2};
        constexpr From lower = static_cast<From>(std::numeric_limits<To>::lowest());
        if constexpr (!std::is_signed_v<To>
                      || std::numeric_limits<From>::digits > std::numeric_limits<To>::digits) {
            if (!(number > lower - From{1} && number < upper)) return false;
        } else {
            if (!(number >= lower && number < upper)) return false;
        }
        converted = static_cast<To>(number);
    } else if constexpr (std::is_signed_v<From>) {
        if constexpr (std::is_signed_v<To>) {
            if constexpr (std::numeric_limits<To>::digits < std::numeric_limits<From>::digits) {
                if (number < std::numeric_limits<To>::lowest()
                    || number > std::numeric_limits<To>::max()) return false;
            }
        } else {
            if (number < 0) return false;
            if constexpr (std::numeric_limits<To>::digits < std::numeric_limits<From>::digits) {
                if (static_cast<std::uint64_t>(number) > std::numeric_limits<To>::max()) return false;
            }
        }
        converted = static_cast<To>(number);
    } else {
        if constexpr (std::numeric_limits<To>::digits < std::numeric_limits<From>::digits) {
            if (number > static_cast<std::uint64_t>(std::numeric_limits<To>::max())) return false;
        }
        converted = static_cast<To>(number);
    }
    result = converted;
    return true;
}

template <class To, auto Make, class From>
TELEMETRY_FORCE_INLINE constexpr bool storeConverted(From number, Scalar& result) noexcept
{
    To converted{};
    if (!convertNumberTo(number, converted)) return false;
    result = Make(converted);
    return true;
}

template <class To, class From>
TELEMETRY_FORCE_INLINE constexpr std::optional<To> readNumber(From number) noexcept
{
    To converted{};
    if (!convertNumberTo(number, converted)) return std::nullopt;
    return converted;
}

template <class Number>
TELEMETRY_FORCE_INLINE constexpr bool convertNumber(Number number, ScalarType target, Scalar& result) noexcept
{
    switch (target) {
        case ScalarType::F32: return storeConverted<float, &Scalar::fromF32>(number, result);
        case ScalarType::F64: return storeConverted<double, &Scalar::fromF64>(number, result);
        case ScalarType::U8: return storeConverted<std::uint8_t, &Scalar::fromU8>(number, result);
        case ScalarType::U16: return storeConverted<std::uint16_t, &Scalar::fromU16>(number, result);
        case ScalarType::U32: return storeConverted<std::uint32_t, &Scalar::fromU32>(number, result);
        case ScalarType::U64: return storeConverted<std::uint64_t, &Scalar::fromU64>(number, result);
        case ScalarType::S8: return storeConverted<std::int8_t, &Scalar::fromS8>(number, result);
        case ScalarType::S16: return storeConverted<std::int16_t, &Scalar::fromS16>(number, result);
        case ScalarType::S32: return storeConverted<std::int32_t, &Scalar::fromS32>(number, result);
        case ScalarType::S64: return storeConverted<std::int64_t, &Scalar::fromS64>(number, result);
        case ScalarType::Bool: return storeConverted<bool, &Scalar::fromBool>(number, result);
        default: return false;
    }
}

} // namespace detail

// Convert between the supported numeric/bool types. On failure result is
// unchanged; it may alias value. Null and unknown destinations are rejected.
// Float -> integer truncates toward zero, then checks the destination bounds.
// Integer -> integer checks bounds without a floating-point intermediate.
// Finite -> bool uses zero/nonzero. NaN/Inf are supported only by float targets.
// Float conversions may round; a finite value outside the target range fails.
TELEMETRY_FORCE_INLINE constexpr bool convertScalar(const Scalar& value, ScalarType target, Scalar& result) noexcept
{
    // The private variant guarantees a valid source tag. Identity conversion
    // preserves the payload, including floating special values; Null is not
    // a numeric write value. Unknown destinations cannot match a source tag.
    if (value.type() == target) {
        if (target == ScalarType::Null) return false;
        result = value;
        return true;
    }
    // Preserve the source's native width; there is no universal double/int64
    // intermediate. Known source/destination pairs fold at the call site.
    switch (value.type()) {
        case ScalarType::F32: return detail::convertNumber(value.get<float>(), target, result);
        case ScalarType::F64: return detail::convertNumber(value.get<double>(), target, result);
        case ScalarType::U8: return detail::convertNumber(value.get<std::uint8_t>(), target, result);
        case ScalarType::U16: return detail::convertNumber(value.get<std::uint16_t>(), target, result);
        case ScalarType::U32: return detail::convertNumber(value.get<std::uint32_t>(), target, result);
        case ScalarType::U64: return detail::convertNumber(value.get<std::uint64_t>(), target, result);
        case ScalarType::S8: return detail::convertNumber(value.get<std::int8_t>(), target, result);
        case ScalarType::S16: return detail::convertNumber(value.get<std::int16_t>(), target, result);
        case ScalarType::S32: return detail::convertNumber(value.get<std::int32_t>(), target, result);
        case ScalarType::S64: return detail::convertNumber(value.get<std::int64_t>(), target, result);
        case ScalarType::Bool: return detail::convertNumber(value.get<bool>(), target, result);
        default: return false;
    }
}

// Select the destination at compile time. This shares the checked conversion
// policy used by writes; an empty result means the value cannot be represented.
template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
[[nodiscard]] TELEMETRY_FORCE_INLINE constexpr std::optional<T> convertScalar(const Scalar& value) noexcept
{
    // Keep both native source and destination types visible. In particular,
    // F32 -> float needs neither a double intermediate nor narrowing checks.
    switch (value.type()) {
        case ScalarType::F32: return detail::readNumber<T>(value.get<float>());
        case ScalarType::F64: return detail::readNumber<T>(value.get<double>());
        case ScalarType::U8: return detail::readNumber<T>(value.get<std::uint8_t>());
        case ScalarType::U16: return detail::readNumber<T>(value.get<std::uint16_t>());
        case ScalarType::U32: return detail::readNumber<T>(value.get<std::uint32_t>());
        case ScalarType::U64: return detail::readNumber<T>(value.get<std::uint64_t>());
        case ScalarType::S8: return detail::readNumber<T>(value.get<std::int8_t>());
        case ScalarType::S16: return detail::readNumber<T>(value.get<std::int16_t>());
        case ScalarType::S32: return detail::readNumber<T>(value.get<std::int32_t>());
        case ScalarType::S64: return detail::readNumber<T>(value.get<std::int64_t>());
        case ScalarType::Bool: return detail::readNumber<T>(value.get<bool>());
        default: return std::nullopt;
    }
}

} // namespace telemetry

#endif
