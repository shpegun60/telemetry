/**
 * @file TelemetryCatalog.h
 * @brief Borrowed, definition-time validated telemetry field groups.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_CATALOG_H
#define TELEMETRY_CATALOG_H

#include <cstddef>

#include "../field/TelemetryField.h"
#include "../field/TelemetryId.h"

namespace telemetry {

struct Catalog {
    const GroupId id = 0;
    const char* const name = "";
    const Field* const fields = nullptr;
    const std::size_t count = 0;

    constexpr Catalog() noexcept = default;

    // Validate once, retaining only the correct contiguous prefix. Counts
    // above 65536 are clipped before indexing or narrowing the row ordinal.
    // The pointer/count must describe a live array; null produces no fields.
    // A template keeps the deleted rvalue-array overload preferred to
    // array-to-pointer conversion in the explicit pointer/count form.
    template <class = void>
    constexpr Catalog(GroupId group, const char* label, const Field* rows,
                      std::size_t requestedCount) noexcept
        : id(group), name(label), fields(rows), count(prefixCount_(group, rows, requestedCount)) {}

    template <std::size_t N>
    constexpr Catalog(GroupId group, const char* label, const Field (&rows)[N]) noexcept
        : Catalog(group, label, static_cast<const Field*>(rows), N) {}

    template <std::size_t N>
    Catalog(GroupId, const char*, const Field (&&)[N]) = delete;
    template <std::size_t N>
    Catalog(GroupId, const char*, const Field (&&)[N], std::size_t) = delete;

private:
    static constexpr std::size_t prefixCount_(GroupId group, const Field* rows,
                                             std::size_t requestedCount) noexcept
    {
        if (rows == nullptr) return 0;
        const std::size_t limit = requestedCount < idComponentCapacity
            ? requestedCount : idComponentCapacity;
        std::size_t accepted = 0;
        while (accepted < limit && rows[accepted].id == makeId(group, static_cast<FieldOffset>(accepted))) {
            ++accepted;
        }
        return accepted;
    }
};

constexpr bool str_equal(const char* a, const char* b) noexcept
{
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

// Optional definition-time validation, never part of lookup/read/write.
// The pointer/count must describe an actual array; nullptr is valid only empty.
constexpr bool names_unique(const Field* fields, std::size_t count) noexcept
{
    if (fields == nullptr) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (fields[i].name == nullptr) return false;
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
    if (catalogs == nullptr) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (catalogs[i].name == nullptr) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (str_equal(catalogs[i].name, catalogs[j].name)) return false;
        }
    }
    return true;
}

} // namespace telemetry

#endif
