/**
 * @file TelemetryCommandCatalog.h
 * @brief Borrowed, definition-time validated command groups.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_CATALOG_H
#define TELEMETRY_COMMAND_CATALOG_H

#include "../core/TelemetryId.h"
#include "TelemetryCommand.h"
#include <cstddef>

namespace telemetry {

// Command IDs use the same packed group/index arithmetic as Field IDs, but
// remain a separate logical ID space. Groups and rows accept only their dense,
// zero-based prefix, so lookup needs two bounds checks and no search.
struct CommandCatalog {
    const GroupId id = 0;
    const char* const name = "";
    const Command* const commands = nullptr;
    const std::size_t count = 0;

    constexpr CommandCatalog() noexcept = default;

    template <class = void>
    constexpr CommandCatalog(GroupId group, const char* label,
                             const Command* rows, std::size_t requestedCount) noexcept
        : id(group), name(label), commands(rows),
          count(prefixCount_(group, rows, requestedCount)) {}

    template <std::size_t N>
    constexpr CommandCatalog(GroupId group, const char* label,
                             const Command (&rows)[N]) noexcept
        : CommandCatalog(group, label, static_cast<const Command*>(rows), N) {}

    template <std::size_t N>
    CommandCatalog(GroupId, const char*, const Command (&&)[N]) = delete;
    template <std::size_t N>
    CommandCatalog(GroupId, const char*, const Command (&&)[N], std::size_t) = delete;

private:
    static constexpr std::size_t prefixCount_(GroupId group, const Command* rows,
                                              std::size_t requestedCount) noexcept
    {
        if (rows == nullptr) return 0;
        const std::size_t limit = requestedCount < idComponentCapacity
            ? requestedCount : idComponentCapacity;
        std::size_t accepted = 0;
        while (accepted < limit
               && rows[accepted].id == makeId(group, static_cast<CommandOffset>(accepted))) {
            ++accepted;
        }
        return accepted;
    }
};

constexpr bool commandStringEqual(const char* a, const char* b) noexcept
{
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}

constexpr bool commandNamesUnique(const Command* commands, std::size_t count) noexcept
{
    if (commands == nullptr) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (commands[i].name == nullptr) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (commandStringEqual(commands[i].name, commands[j].name)) return false;
        }
    }
    return true;
}

constexpr bool commandCatalogNamesUnique(const CommandCatalog* catalogs,
                                         std::size_t count) noexcept
{
    if (catalogs == nullptr) return count == 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (catalogs[i].name == nullptr) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (commandStringEqual(catalogs[i].name, catalogs[j].name)) return false;
        }
    }
    return true;
}

} // namespace telemetry

#endif
