/**
 * @file TelemetryCommandCatalogView.h
 * @brief Positional command/catalog views without stored descriptor IDs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_CATALOG_VIEW_H
#define TELEMETRY_COMMAND_CATALOG_VIEW_H

#include "TelemetryCommandCatalog.h"
#include "../detail/TelemetryIndexedRange.h"

namespace telemetry {
class CommandEntryView {
    template <class> friend class detail::IndexedViewIterator;
    const Command* command_;
    GroupId group_;
    EntryOffset index_;
    constexpr CommandEntryView(GroupId group, EntryOffset index, const Command& value) noexcept
        : command_(std::addressof(value)), group_(group), index_(index) {}
public:
    using Descriptor = Command;
    constexpr EntryOffset index() const noexcept { return index_; }
    constexpr CommandId id() const noexcept { return makeId(group_, index_); }
    constexpr const Command& command() const noexcept { return *command_; }
};
using CommandRange = detail::IndexedViewRange<CommandEntryView>;

class CommandCatalogView {
    template <class> friend class detail::IndexedViewIterator;
    const CommandCatalog* catalog_;
    GroupId index_;
    constexpr CommandCatalogView(GroupId, EntryOffset index, const CommandCatalog& value) noexcept
        : catalog_(std::addressof(value)), index_(index) {}
public:
    using Descriptor = CommandCatalog;
    constexpr GroupId index() const noexcept { return index_; }
    constexpr const char* name() const noexcept { return catalog_->name; }
    constexpr const CommandCatalog& catalog() const noexcept { return *catalog_; }
    // A range owns only the traversal state; the source descriptors remain
    // borrowed even if this small catalog view is copied or destroyed.
    constexpr CommandRange commands() const noexcept { return {catalog_->commands, catalog_->count, index_}; }
};
using CommandCatalogRange = detail::IndexedViewRange<CommandCatalogView>;
} // namespace telemetry
#endif
