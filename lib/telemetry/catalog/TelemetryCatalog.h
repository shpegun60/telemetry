/**
 * @file TelemetryCatalog.h
 * @brief Borrowed, definition-time validated telemetry field groups.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_CATALOG_H
#define TELEMETRY_CATALOG_H

#include <cstddef>

#include "../core/TelemetryId.h"
#include "../field/TelemetryField.h"

namespace telemetry {

// A catalog borrows both the array and its text; construction never copies
// descriptors or extends any lifetime. Keep those objects alive and unchanged
// while an index or serializer can observe this view.
struct Catalog {
    const char* const name = "";
    const Field* const fields = nullptr;
    const std::size_t count = 0;

    constexpr Catalog() noexcept = default;

    // Position is identity. Counts above 65536 are clipped before indexing.
    // The pointer/count must describe a live array; null produces no fields.
    // A template keeps the deleted rvalue-array overload preferred to
    // array-to-pointer conversion in the explicit pointer/count form.
    template <class = void>
    constexpr Catalog(const char* label, const Field* rows,
                      std::size_t requestedCount) noexcept
        : name(label), fields(rows), count(clippedCount_(rows, requestedCount)) {}

    template <std::size_t N>
    constexpr Catalog(const char* label, const Field (&rows)[N]) noexcept
        : Catalog(label, static_cast<const Field*>(rows), N) {}

    template <std::size_t N>
    Catalog(const char*, const Field (&&)[N]) = delete;
    template <std::size_t N>
    Catalog(const char*, const Field (&&)[N], std::size_t) = delete;

    constexpr const Field* begin() const noexcept { return fields; }
    constexpr const Field* end() const noexcept { return detail::pointerPresent(fields) ? fields + count : nullptr; }
    constexpr std::size_t size() const noexcept { return count; }
    constexpr bool empty() const noexcept { return count == 0; }

private:
    static constexpr std::size_t clippedCount_(const Field* rows,
                                               std::size_t requestedCount) noexcept
    {
        if (!detail::pointerPresent(rows)) return 0;
        return requestedCount < idComponentCapacity
            ? requestedCount : idComponentCapacity;
    }
};

// The validation loops check nulls first; both inputs here must be valid
// NUL-terminated strings. Compare contents, not string-literal addresses.
constexpr bool str_equal(const char* a, const char* b) noexcept
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

// Optional quadratic definition-time validation, never part of lookup/read/write.
// The pointer/count must describe an actual array; nullptr is valid only empty.
constexpr bool names_unique(const Field* fields, std::size_t count) noexcept
{
    if (!detail::pointerPresent(fields)) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!detail::pointerPresent(fields[i].name)) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (str_equal(fields[i].name, fields[j].name)) {
                return false;
            }
        }
    }
    return true;
}

// Optional definition-time validation, never part of lookup/read/write.
// The pointer/count must describe an actual array; nullptr is valid only empty.
constexpr bool catalog_names_unique(const Catalog* catalogs, std::size_t count) noexcept
{
    if (!detail::pointerPresent(catalogs)) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!detail::pointerPresent(catalogs[i].name)) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (str_equal(catalogs[i].name, catalogs[j].name)) return false;
        }
    }
    return true;
}

} // namespace telemetry

#endif
