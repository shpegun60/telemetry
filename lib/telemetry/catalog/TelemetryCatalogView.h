/**
 * @file TelemetryCatalogView.h
 * @brief Positional field/catalog views with computed IDs and borrowed metadata.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_CATALOG_VIEW_H
#define TELEMETRY_CATALOG_VIEW_H

#include "TelemetryCatalog.h"
#include "../detail/TelemetryIndexedRange.h"

namespace telemetry {
class FieldEntryView {
    template <class> friend class detail::IndexedViewIterator;
    const Field* field_;
    GroupId group_;
    EntryOffset index_;
    constexpr FieldEntryView(GroupId group, EntryOffset index, const Field& value) noexcept
        : field_(std::addressof(value)), group_(group), index_(index) {}
public:
    using Descriptor = Field;
    constexpr EntryOffset index() const noexcept { return index_; }
    constexpr FieldId id() const noexcept { return makeId(group_, index_); }
    constexpr const Field& field() const noexcept { return *field_; }
};
using FieldRange = detail::IndexedViewRange<FieldEntryView>;

class FieldCatalogView {
    template <class> friend class detail::IndexedViewIterator;
    const Catalog* catalog_;
    GroupId index_;
    constexpr FieldCatalogView(GroupId, EntryOffset index, const Catalog& value) noexcept
        : catalog_(std::addressof(value)), index_(index) {}
public:
    using Descriptor = Catalog;
    constexpr GroupId index() const noexcept { return index_; }
    constexpr const char* name() const noexcept { return catalog_->name; }
    constexpr const Catalog& catalog() const noexcept { return *catalog_; }
    // The range copies its group/index context; it never borrows this view.
    // Catalog, rows and their strings must outlive every returned view/range.
    constexpr FieldRange fields() const noexcept
    { return FieldRange::fromCapped(catalog_->fields, catalog_->count, index_); }
};
using FieldCatalogRange = detail::IndexedViewRange<FieldCatalogView>;
} // namespace telemetry
#endif
