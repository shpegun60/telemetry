/**
 * @file TelemetryJsonValue.h
 * @brief Internal Scalar, metadata, bounds and enum JSON serialization.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 *
 * This header is an implementation detail and is not a stable public API.
 */
#ifndef TELEMETRY_DETAIL_JSON_VALUE_H
#define TELEMETRY_DETAIL_JSON_VALUE_H

#include "TelemetryJsonWriter.h"
#include "../core/TelemetryConversion.h"
#include "../field/TelemetryFieldType.h"

#include <cmath>
#include <cstdint>
#include <inttypes.h>
#include <limits>
#include <string_view>

namespace telemetry {
namespace detail {

inline constexpr const char* scalarTypeName(const ScalarType type) noexcept
{
    switch (type) {
        case ScalarType::Null: return "null";
        case ScalarType::F32: return "f32";
        case ScalarType::F64: return "f64";
        case ScalarType::U8: return "u8";
        case ScalarType::U16: return "u16";
        case ScalarType::U32: return "u32";
        case ScalarType::S8: return "s8";
        case ScalarType::S16: return "s16";
        case ScalarType::S32: return "s32";
        case ScalarType::U64: return "u64";
        case ScalarType::S64: return "s64";
        case ScalarType::Bool: return "bool";
        default: return "?";
    }
}


// Wire values preserve the stored numeric alternative. JSON has no NaN or
// infinity literal, so unavailable and non-finite readings share null.
// This formatting step never performs field validation or invokes callbacks.
static inline bool appendScalar(JsonWriter& out, const Scalar& value) noexcept
{
    if (value.type() == ScalarType::Null) {
        return out.append("null");
    }

    switch (value.type()) {
        case ScalarType::F32:
            if (std::isfinite(value.get<float>())) {
                return out.appendFloating(value.get<float>(), std::numeric_limits<float>::max_digits10);
            }
            return out.append("null");
        case ScalarType::F64:
            if (std::isfinite(value.get<double>())) {
                return out.appendFloating(value.get<double>(), std::numeric_limits<double>::max_digits10);
            }
            return out.append("null");
        case ScalarType::U8:
            return out.append("%u", static_cast<unsigned>(value.get<std::uint8_t>()));
        case ScalarType::U16:
            return out.append("%u", static_cast<unsigned>(value.get<std::uint16_t>()));
        case ScalarType::U32:
            return out.append("%" PRIu32, value.get<std::uint32_t>());
        case ScalarType::S8:
            return out.append("%d", static_cast<int>(value.get<std::int8_t>()));
        case ScalarType::S16:
            return out.append("%d", static_cast<int>(value.get<std::int16_t>()));
        case ScalarType::S32:
            return out.append("%" PRId32, value.get<std::int32_t>());
        case ScalarType::U64:
            return out.quoteInt64()
                ? out.appendQuotedInteger(value.get<std::uint64_t>())
                : out.appendInteger(value.get<std::uint64_t>());
        case ScalarType::S64:
            return out.quoteInt64()
                ? out.appendQuotedInteger(value.get<std::int64_t>())
                : out.appendInteger(value.get<std::int64_t>());
        case ScalarType::Bool:
            return out.append(value.get<bool>() ? "true" : "false");
        default:
            return out.append("null");
    }
}

static TELEMETRY_FORCE_INLINE bool appendMetadata(JsonWriter& out,
                                                  const Scalar& value) noexcept
{
    // A client commonly parses schema numbers into double. Nine F32 digits
    // can round FLT_MAX above its exact value, making the advertised maximum
    // fail a checked double -> float write. Preserve the promoted value.
    if (value.type() == ScalarType::F32) {
        return out.appendFloating(value.get<float>(), std::numeric_limits<double>::max_digits10);
    }
    return appendScalar(out, value);
}

template <class T>
TELEMETRY_FORCE_INLINE bool appendNumericBound(JsonWriter& out, const Scalar& value,
                                               bool minimum) noexcept
{
    const T native = minimum ? std::numeric_limits<T>::lowest() : std::numeric_limits<T>::max();
    return value.get<T>() == native ? out.append("null") : appendMetadata(out, value);
}

static inline bool appendBound(JsonWriter& out, const Scalar& value, bool minimum,
                               bool enumBound) noexcept
{
    // Schema-only shorthand: null means the native endpoint of the numeric
    // type. Compare each endpoint independently and without widening to double.
    // Enum and Bool bounds are always explicit, even at native endpoints.
    if (!enumBound) {
        switch (value.type()) {
            case ScalarType::F32: return appendNumericBound<float>(out, value, minimum);
            case ScalarType::F64: return appendNumericBound<double>(out, value, minimum);
            case ScalarType::U8: return appendNumericBound<std::uint8_t>(out, value, minimum);
            case ScalarType::U16: return appendNumericBound<std::uint16_t>(out, value, minimum);
            case ScalarType::U32: return appendNumericBound<std::uint32_t>(out, value, minimum);
            case ScalarType::U64: return appendNumericBound<std::uint64_t>(out, value, minimum);
            case ScalarType::S8: return appendNumericBound<std::int8_t>(out, value, minimum);
            case ScalarType::S16: return appendNumericBound<std::int16_t>(out, value, minimum);
            case ScalarType::S32: return appendNumericBound<std::int32_t>(out, value, minimum);
            case ScalarType::S64: return appendNumericBound<std::int64_t>(out, value, minimum);
            default: break;
        }
    }
    return appendMetadata(out, value);
}

struct EnumJsonContext {
    JsonWriter& out;
    bool first = true;
};

static inline bool appendEnumEntry(void* context, const Scalar& value, std::string_view name) noexcept
{
    // Dictionary keys are always decimal strings, independently of the option
    // that quotes 64-bit numeric values elsewhere. The sink is synchronous;
    // neither this callback nor JsonWriter retains the borrowed name view.
    auto& state = *static_cast<EnumJsonContext*>(context);
    if (!state.first && !state.out.append(",")) return false;
    state.first = false;
    if (!state.out.append("\"")) return false;
    // Enum descriptions only produce supported integral alternatives. The
    // only one that may not fit int64_t is the upper half of uint64_t.
    if (const auto signedCode = convertScalar<std::int64_t>(value)) {
        if (!state.out.appendInteger(*signedCode)) return false;
    } else if (!state.out.appendInteger(value.get<std::uint64_t>())) return false;
    return state.out.append("\":") && state.out.appendString(name);
}

} // namespace detail
} // namespace telemetry

#endif
