/**
 * @file TelemetryFieldCatalogTable.h
 * @brief Position-identified groups with native static and dynamic global access.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_CATALOG_TABLE_H
#define TELEMETRY_FIELD_CATALOG_TABLE_H
#include "../field/TelemetryFieldTable.h"
#include "TelemetryIndex.h"
#include "TelemetryGroup.h"
#include <array>
#include <tuple>
namespace telemetry {
template <class... Groups>
class FieldCatalogTable {
    static_assert((detail::IsTableGroup<Groups, Field>::value && ...),
                  "FieldCatalogTable requires group(name, FieldTable) entries");
    static_assert(sizeof...(Groups) <= idComponentCapacity,
                  "Catalog table exceeds the 16-bit group capacity");
    std::tuple<const typename Groups::TableType*...> tables_;
    std::array<Catalog, sizeof...(Groups)> catalogs_;
public:
    constexpr explicit FieldCatalogTable(Groups... groups) noexcept
        : tables_(groups.table...),
          catalogs_{Catalog{groups.name, groups.table->data(), groups.table->size()}...} {}
    FieldCatalogTable(const FieldCatalogTable&) = delete;
    FieldCatalogTable(FieldCatalogTable&&) = delete;
    FieldCatalogTable& operator=(const FieldCatalogTable&) = delete;
    FieldCatalogTable& operator=(FieldCatalogTable&&) = delete;
    constexpr const Catalog* data() const & noexcept { return catalogs_.data(); }
    const Catalog* data() const && = delete;
    constexpr std::size_t size() const noexcept { return catalogs_.size(); }
    constexpr const Catalog& operator[](std::size_t i) const & noexcept { return catalogs_[i]; }
    const Catalog& operator[](std::size_t) const && = delete;
    constexpr CatalogIndex index() const & noexcept { return {catalogs_.data(), catalogs_.size()}; }
    CatalogIndex index() const && = delete;
    constexpr operator CatalogIndex() const & noexcept { return index(); }
    operator CatalogIndex() const && = delete;
    TELEMETRY_FORCE_INLINE constexpr const Field* find(FieldId id) const & noexcept
    { return index().find(id); }
    const Field* find(FieldId) const && = delete;
    template <FieldId Id>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto read() const noexcept
    {
        static_assert(groupOf(Id) < sizeof...(Groups), "Typed field group is outside FieldCatalogTable");
        if constexpr (groupOf(Id) < sizeof...(Groups))
            return std::get<groupOf(Id)>(tables_)->template read<indexOf(Id)>();
    }
    template <FieldId Id, class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE std::optional<T> read() const noexcept
    {
        static_assert(groupOf(Id) < sizeof...(Groups), "Typed field group is outside FieldCatalogTable");
        if constexpr (groupOf(Id) < sizeof...(Groups))
            return std::get<groupOf(Id)>(tables_)->template read<indexOf(Id), T>();
        else return std::nullopt;
    }
    template <FieldId Id, class T,
              std::enable_if_t<detail::isScalarNumber<T> || std::is_same_v<T, Scalar>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE WriteResult write(T value) const noexcept
    {
        static_assert(groupOf(Id) < sizeof...(Groups), "Typed field group is outside FieldCatalogTable");
        if constexpr (groupOf(Id) < sizeof...(Groups))
            return std::get<groupOf(Id)>(tables_)->template write<indexOf(Id)>(value);
        else return WriteResult::NotFound;
    }
    [[nodiscard]] TELEMETRY_FORCE_INLINE Scalar read(FieldId id) const noexcept
    { return index().read(id); }
    template <class T>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto read(FieldId id) const noexcept
        -> decltype(std::declval<CatalogIndex>().template read<T>(id))
    { return index().template read<T>(id); }
    template <class T>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto write(FieldId id, T value) const noexcept
        -> decltype(std::declval<CatalogIndex>().write(id, value))
    { return index().write(id, value); }
};
template <class... Groups> FieldCatalogTable(Groups...) -> FieldCatalogTable<std::decay_t<Groups>...>;
} // namespace telemetry
#endif
