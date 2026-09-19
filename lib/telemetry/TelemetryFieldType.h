/**
 * @file TelemetryFieldType.h
 * @brief Numeric field types with optional enumeration metadata for schema consumers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 */
#ifndef TELEMETRY_FIELD_TYPE_H
#define TELEMETRY_FIELD_TYPE_H

#include "TelemetryScalar.h"
#include <string_view>

namespace telemetry {

// The value and name are borrowed for the duration of the sink call. Return false
// to stop enumeration immediately. No source getter or setter is involved.
using EnumEntrySink = bool (*)(void*, const Scalar&, std::string_view) noexcept;
using EnumDescription = bool (*)(void*, EnumEntrySink) noexcept;

class FieldType {
public:
    // Implicit construction preserves rows containing ScalarType::F32, etc.
    constexpr FieldType(ScalarType type = ScalarType::Null) noexcept : valueType_(type) {}

    TELEMETRY_FORCE_INLINE constexpr operator ScalarType() const noexcept { return valueType_; }
    constexpr bool hasEnum() const noexcept { return describe_ != nullptr; }

    // A numeric-only descriptor or an absent sink returns false. A context
    // may be null when the supplied sink does not need application state.
    bool describeEnum(void* context, EnumEntrySink sink) const noexcept
    {
        return describe_ != nullptr && sink != nullptr && describe_(context, sink);
    }

private:
    constexpr FieldType(ScalarType type, EnumDescription describe) noexcept
        : valueType_(type), describe_(describe) {}

    template <class E, E... Values>
    friend constexpr FieldType enumType() noexcept;

    ScalarType valueType_ = ScalarType::Null;
    EnumDescription describe_ = nullptr;
};

static_assert(std::is_trivially_copyable_v<FieldType>, "Field type metadata must remain trivial");

} // namespace telemetry

#endif
