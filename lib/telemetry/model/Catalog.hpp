/*
 * @file Catalog.hpp
 * @brief Shared borrowed groups and per-row type references for Model catalogs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Common borrowed group declarations and per-row structural type references.
 *
 * A group labels a stable local table; declaration order determines its
 * catalog position. TypeId arrays are created later against the complete
 * Model registry, so local tables need no independently assigned global type
 * numbers and carry no semantic parameter metadata.
 */

#ifndef TELEMETRY_MODEL_CATALOG_HPP
#define TELEMETRY_MODEL_CATALOG_HPP
#pragma once

#include "../type/Descriptor.hpp"
#include "../detail/Name.hpp"
#include <concepts>
#include <cstdlib>
#include <memory>
#include <type_traits>

namespace telemetry {

// A declaration-time label and borrowed local-table address. Construct through
// group() so the name is validated and the table argument is an actual lvalue.
// The table and immutable name must outlive all catalogs built from this group.
template<class Table>
struct TableGroup {
	using TableType = Table;
	const char* name;
	const Table* table;
};

// The group borrows a stable local table. Its catalog position is its ID.
template<class... Explicit, class Table>
    requires(sizeof...(Explicit) == 0)
[[nodiscard]] constexpr auto group(detail::Name name, Table& table) noexcept
{
	return TableGroup<std::remove_cv_t<Table>>{name, std::addressof(table)};
}

// Field and Command each refer to one value/request TypeId per row.
struct ValueTypeCatalog {
	const TypeId* entries;
	std::uint32_t count;
};

} // namespace telemetry

#endif
