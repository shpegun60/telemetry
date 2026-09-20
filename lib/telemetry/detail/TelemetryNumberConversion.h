/**
 * @file TelemetryNumberConversion.h
 * @brief Internal checked conversion primitives for native numeric types.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 *
 * This header is an implementation detail and is not a stable public API.
 */
#ifndef TELEMETRY_DETAIL_NUMBER_CONVERSION_H
#define TELEMETRY_DETAIL_NUMBER_CONVERSION_H

#include "../core/TelemetryCompiler.h"
#include "../core/TelemetryScalar.h"

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
// Keep this small check inline even at -Os: an outlined call can make generic
// readers preserve an FPU register across the rare float-to-bool conversion.
TELEMETRY_FORCE_INLINE constexpr bool scalarFinite(Number number) noexcept
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
        if constexpr (std::is_signed_v<To>) {
            static_assert(std::numeric_limits<To>::lowest()
                          == -std::numeric_limits<To>::max() - 1,
                          "Floating-to-integer conversion requires a full two's-complement range");
        }
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
} // namespace telemetry

#endif
