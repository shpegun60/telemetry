/*
 * @file Catalog.hpp
 * @brief Shared borrowed groups and per-row type references for Model catalogs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_MODEL_CATALOG_HPP
#define TELEMETRY_STRUCTURED_MODEL_CATALOG_HPP

#include "../type/Descriptor.hpp"
#include <concepts>
#include <cstdlib>
#include <memory>
#include <type_traits>

namespace telemetry::structured {

template <class Table>
struct TableGroup {
    using TableType = Table;
    const char* name;
    const Table* table;
};

// The group borrows a stable local table. Its catalog position is its ID.
template <class... Explicit, class Table>
    requires (sizeof...(Explicit) == 0)
[[nodiscard]] constexpr auto group(const char* name, Table& table) noexcept
{
    if (name == nullptr || name[0] == '\0') std::abort();
    return TableGroup<std::remove_cv_t<Table>>{name, std::addressof(table)};
}

// Field and Command each refer to one value/request TypeId per row.
struct ValueTypeCatalog {
    const TypeId* entries;
    std::uint32_t count;
};

} // namespace telemetry::structured

#endif
