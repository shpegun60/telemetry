/**
 * @file TelemetryConversion.h
 * @brief Public checked Scalar conversion API used by field reads and writes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_CONVERSION_H
#define TELEMETRY_CONVERSION_H

#include "TelemetryCompiler.h"
#include "TelemetryScalar.h"
#include "../detail/TelemetryNumberConversion.h"

#include <optional>

namespace telemetry {

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
