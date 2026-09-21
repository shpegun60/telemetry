/**
 * @file Json.hpp
 * @brief Adapter-local metadata encoding using only public telemetry types.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "Stream.hpp"
#include <telemetry/field/TelemetryFieldType.h>
#include <cmath>
#include <variant>

namespace telemetry_resource::detail
{
inline const char* typeName(telemetry::ScalarType type) noexcept
{
    using T = telemetry::ScalarType;
    switch (type)
    {
        case T::Null:
            return "null";
        case T::F32:
            return "f32";
        case T::F64:
            return "f64";
        case T::U8:
            return "u8";
        case T::U16:
            return "u16";
        case T::U32:
            return "u32";
        case T::U64:
            return "u64";
        case T::S8:
            return "s8";
        case T::S16:
            return "s16";
        case T::S32:
            return "s32";
        case T::S64:
            return "s64";
        case T::Bool:
            return "bool";
    }
    return "?";
}

inline bool scalar(Writer& out, const telemetry::Scalar& value) noexcept
{
    return value.visit(
        [&out](auto number) noexcept
        {
            using T = decltype(number);
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return out.text("null");
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return out.text(number ? "true" : "false");
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                return std::isfinite(number) ? out.floating(number) : out.text("null");
            }
            else
            {
                return out.integer(number);
            }
        });
}

inline bool bound(Writer& out, const telemetry::Scalar& value, bool minimum, bool isEnum) noexcept
{
    if (!isEnum)
    {
        const bool native = value.visit(
            [minimum](auto number) noexcept
            {
                using T = decltype(number);
                if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, bool>)
                {
                    return false;
                }
                else
                {
                    return number == (minimum ? std::numeric_limits<T>::lowest()
                                              : std::numeric_limits<T>::max());
                }
            });
        if (native)
        {
            return out.text("null");
        }
    }
    return scalar(out, value);
}

inline bool dictionary(Writer& out, const telemetry::FieldType& type) noexcept
{
    struct Context
    {
        Writer& out;
        bool first = true;
    } context{out};

    return type.describeEnum(
        &context,
        +[](void* raw, const telemetry::Scalar& code, std::string_view name) noexcept
        {
            auto& s = *static_cast<Context*>(raw);
            if (!s.first && !s.out.text(","))
            {
                return false;
            }
            s.first = false;
            if (!s.out.text("\""))
            {
                return false;
            }
            const bool emitted = code.visit(
                [&s](auto number) noexcept
                {
                    using T = decltype(number);
                    if constexpr (std::is_integral_v<T>)
                    {
                        return s.out.integer(number);
                    }
                    else
                    {
                        return s.out.fail();
                    }
                });
            return emitted && s.out.text("\":") && s.out.string(name);
        });
}

inline bool limits(Writer& out, const telemetry::FieldType& type) noexcept
{
    const bool isEnum = type.hasEnum();
    if (!out.text(",\"min\":") || !bound(out, type.minimum(), true, isEnum) ||
        !out.text(",\"max\":") || !bound(out, type.maximum(), false, isEnum) ||
        !out.text(",\"default\":") || !scalar(out, type.defaultValue()))
    {
        return false;
    }
    if (isEnum && (!out.text(",\"enum\":{") || !dictionary(out, type) || !out.text("}")))
    {
        return false;
    }
    return true;
}
} // namespace telemetry_resource::detail
