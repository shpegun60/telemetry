#ifndef TELEMETRY_CATALOG_H
#define TELEMETRY_CATALOG_H

#include <cstddef>
#include <cstdint>

#include "TelemetryGetter.h"

namespace telemetry {

// Stable point identity, unique across every catalog in a published set.
// All values, including zero, are usable; an ID is not a row index.
using FieldId = std::uint32_t;

// Catalogs describe values; getters own their acquisition semantics.
// Getter applies telemetry's noexcept/empty-value policy to tiny::delegate_ref.
// This layer adds no synchronization or cross-field snapshot guarantee.
// A getter must be noexcept and return null when its value is unavailable.
// All referenced objects, arrays and strings must outlive their consumers.
struct Field {
    FieldId id;
    const char* name;
    const char* unit;
    ScalarType declaredType;
    Getter get;
};

struct Catalog {
    const char* name;
    const Field* fields;
    std::size_t count;
};

constexpr bool str_equal(const char* a, const char* b) noexcept
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

constexpr bool names_unique(const Field* fields, std::size_t count) noexcept
{
    for (std::size_t i = 1; i < count; ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (str_equal(fields[i].name, fields[j].name)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace telemetry

#endif
