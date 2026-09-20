/**
 * @file TelemetryCommandCatalogTable.h
 * @brief Position-identified groups with native static and dynamic global access.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_CATALOG_TABLE_H
#define TELEMETRY_COMMAND_CATALOG_TABLE_H
#include "TelemetryCommandTable.h"
#include "TelemetryCommandCatalogIndex.h"
#include "../catalog/TelemetryGroup.h"
#include <array>
#include <tuple>
namespace telemetry {
template <class... Groups>
class CommandCatalogTable {
    static_assert((detail::IsTableGroup<Groups, Command>::value && ...),
                  "CommandCatalogTable requires group(name, CommandTable) entries");
    static_assert(sizeof...(Groups) <= idComponentCapacity,
                  "Catalog table exceeds the 16-bit group capacity");
    std::tuple<const typename Groups::TableType*...> tables_;
    std::array<CommandCatalog, sizeof...(Groups)> catalogs_;
public:
    constexpr explicit CommandCatalogTable(Groups... groups) noexcept
        : tables_(groups.table...),
          catalogs_{CommandCatalog{groups.name, groups.table->data(), groups.table->size()}...} {}
    CommandCatalogTable(const CommandCatalogTable&) = delete;
    CommandCatalogTable(CommandCatalogTable&&) = delete;
    CommandCatalogTable& operator=(const CommandCatalogTable&) = delete;
    CommandCatalogTable& operator=(CommandCatalogTable&&) = delete;
    constexpr const CommandCatalog* data() const & noexcept { return catalogs_.data(); }
    const CommandCatalog* data() const && = delete;
    constexpr std::size_t size() const noexcept { return catalogs_.size(); }
    constexpr const CommandCatalog& operator[](std::size_t i) const & noexcept { return catalogs_[i]; }
    const CommandCatalog& operator[](std::size_t) const && = delete;
    constexpr CommandCatalogIndex index() const & noexcept { return {catalogs_.data(), catalogs_.size()}; }
    CommandCatalogIndex index() const && = delete;
    constexpr operator CommandCatalogIndex() const & noexcept { return index(); }
    operator CommandCatalogIndex() const && = delete;
    TELEMETRY_FORCE_INLINE constexpr const Command* find(CommandId id) const & noexcept
    { return index().find(id); }
    const Command* find(CommandId) const && = delete;
    template <CommandId Id, class... Input>
    [[nodiscard]] TELEMETRY_FORCE_INLINE CommandResult call(Input... values) const noexcept
    {
        static_assert(groupOf(Id) < sizeof...(Groups), "Typed command group is outside CommandCatalogTable");
        if constexpr (groupOf(Id) < sizeof...(Groups))
            return std::get<groupOf(Id)>(tables_)->template call<indexOf(Id)>(values...);
        else return CommandResult::NotFound;
    }
    [[nodiscard]] TELEMETRY_FORCE_INLINE CommandResult execute(
        CommandId id, const Scalar* values, std::size_t count) const noexcept
    { return index().execute(id, values, count); }
    template <class... Input>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto call(CommandId id, Input... values) const noexcept
        -> decltype(std::declval<CommandCatalogIndex>().call(id, values...))
    { return index().call(id, values...); }
};
template <class... Groups> CommandCatalogTable(Groups...) -> CommandCatalogTable<std::decay_t<Groups>...>;
} // namespace telemetry
#endif
