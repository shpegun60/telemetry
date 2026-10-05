/*
 * @file Traversal.hpp
 * @brief Typed traversal of borrowed definitions, without owning value storage.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Typed visitation of heterogeneous definitions and grouped local tables.
 *
 * Visitors and definitions are borrowed and no endpoint value is materialized.
 * Complete scans discard callback results; stoppable scans require exact bool.
 * Ordered folds implement typed scans;
 * checked runtime positions select generated thunk tables. Visitor exceptions,
 * when enabled, propagate to the caller.
 */

#ifndef TELEMETRY_DETAIL_TRAVERSAL_HPP
#define TELEMETRY_DETAIL_TRAVERSAL_HPP
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <telemetry/core/Id.hpp>

namespace telemetry::detail {

// A visitor stays alive in the caller. Every branch borrows the original
// definition and invokes the same visitor as an lvalue; nothing is copied.
// Callback results are ignored. Exceptions, if enabled, propagate normally.
// Public methods:
// - invoke(): Visit selected definition.
// - make(): Build visitation branches.
template<class Tuple, class Visitor>
struct DefinitionDispatch {
	using Invoke = void (*)(const Tuple&, Visitor&);

	template<std::size_t I>
	static void invoke(const Tuple& definitions, Visitor& visitor)
	{
		(void)std::invoke(visitor, std::get<I>(definitions));
	}

	template<std::size_t... I>
	static consteval auto make(std::index_sequence<I...>) noexcept
	{
		return std::array<Invoke, sizeof...(I)>{{&invoke<I>...}};
	}

	// One table per tuple/visitor type, emitted only when visit() is used.
	// Checked positional indexing, rather than a linear fold, selects a thunk.
	inline static constexpr auto entries =
	    make(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

template<std::size_t I, class Tuple, class Visitor>
constexpr void forEachDefinition(const Tuple& definitions, Visitor& visitor)
{
	const auto& definition = std::get<I>(definitions);
	if constexpr (requires { visitor.template operator()<I>(definition); })
		(void)visitor.template operator()<I>(definition);
	else
		(void)std::invoke(visitor, definition);
}

template<class Tuple, class Visitor, std::size_t... I>
constexpr void forEachDefinition(const Tuple& definitions, Visitor& visitor,
                                 std::index_sequence<I...>)
{
	// The comma fold preserves declaration order, including an empty tuple.
	(forEachDefinition<I>(definitions, visitor), ...);
}

template<std::size_t I, class Tuple, class Visitor>
constexpr bool forEachDefinitionWhile(const Tuple& definitions, Visitor& visitor)
{
	const auto& definition = std::get<I>(definitions);
	if constexpr (requires { visitor.template operator()<I>(definition); }) {
		static_assert(std::is_same_v<decltype(visitor.template operator()<I>(definition)), bool>,
		              "forEachWhile visitor must return exactly bool");
		return visitor.template operator()<I>(definition);
	} else {
		static_assert(std::is_same_v<std::invoke_result_t<Visitor&, decltype(definition)>, bool>,
		              "forEachWhile visitor must return exactly bool");
		return std::invoke(visitor, definition);
	}
}

template<class Tuple, class Visitor, std::size_t... I>
constexpr bool forEachDefinitionWhile(const Tuple& definitions, Visitor& visitor,
                                      std::index_sequence<I...>)
{
	// Every typed branch is instantiated, but false skips subsequent invocations.
	// The empty conjunction is true: there is no unfinished traversal.
	return (forEachDefinitionWhile<I>(definitions, visitor) && ...);
}

// Adds group name/positions while borrowing the original visitor.
// Public methods:
// - operator(): Visit grouped definition.
template<std::size_t Group, class Visitor>
struct GroupVisitor {
	std::string_view name;
	Visitor& visitor;

	template<std::size_t Entry, class Definition>
	constexpr void operator()(const Definition& definition) const
	{
		if constexpr (requires { visitor.template operator()<Group, Entry>(name, definition); })
			(void)visitor.template operator()<Group, Entry>(name, definition);
		else
			(void)std::invoke(visitor, name, definition);
	}
};

template<std::size_t Group, class Tuple, class Visitor>
constexpr void forEachGroup(const Tuple& groups, Visitor& visitor)
{
	const auto& group = std::get<Group>(groups);
	GroupVisitor<Group, Visitor> grouped{group.name, visitor};
	group.table->forEach(grouped);
}

template<class Tuple, class Visitor, std::size_t... Group>
constexpr void forEachGroup(const Tuple& groups, Visitor& visitor, std::index_sequence<Group...>)
{
	(forEachGroup<Group>(groups, visitor), ...);
}

// Preserves grouped typed positions while forwarding the continuation result.
// Public methods:
// - operator(): Visit grouped definition until false.
template<std::size_t Group, class Visitor>
struct GroupWhileVisitor {
	std::string_view name;
	Visitor& visitor;

	template<std::size_t Entry, class Definition>
	constexpr bool operator()(const Definition& definition) const
	{
		if constexpr (requires { visitor.template operator()<Group, Entry>(name, definition); }) {
			static_assert(
			    std::is_same_v<
			        decltype(visitor.template operator()<Group, Entry>(name, definition)), bool>,
			    "forEachWhile visitor must return exactly bool");
			return visitor.template operator()<Group, Entry>(name, definition);
		} else {
			static_assert(std::is_same_v<decltype(std::invoke(visitor, name, definition)), bool>,
			              "forEachWhile visitor must return exactly bool");
			return std::invoke(visitor, name, definition);
		}
	}
};

template<std::size_t Group, class Tuple, class Visitor>
constexpr bool forEachGroupWhile(const Tuple& groups, Visitor& visitor)
{
	const auto& group = std::get<Group>(groups);
	GroupWhileVisitor<Group, Visitor> grouped{group.name, visitor};
	return group.table->forEachWhile(grouped);
}

template<class Tuple, class Visitor, std::size_t... Group>
constexpr bool forEachGroupWhile(const Tuple& groups, Visitor& visitor,
                                 std::index_sequence<Group...>)
{
	return (forEachGroupWhile<Group>(groups, visitor) && ...);
}

// The catalog array and its entry arrays are borrowed coherent ID-space rows.
// Flat erased traversal never materializes native values or executes endpoints.
template<class Catalog, class Visitor>
constexpr void forEachCatalogEntry(const Catalog* catalogs, std::uint32_t count, Visitor& visitor)
{
	for (std::uint32_t group = 0; group < count; ++group) {
		const auto& catalog = catalogs[group];
		for (std::uint32_t entry = 0; entry < catalog.count; ++entry)
			(void)std::invoke(visitor, (static_cast<PackedId>(group) << 16) | entry,
			                  std::string_view{catalog.name}, catalog.entries[entry]);
	}
}

template<class Catalog, class Visitor>
constexpr bool forEachCatalogEntryWhile(const Catalog* catalogs, std::uint32_t count,
                                        Visitor& visitor)
{
	using Entry = decltype(catalogs[0].entries[0]);
	static_assert(
	    std::is_same_v<std::invoke_result_t<Visitor&, PackedId, std::string_view, Entry>, bool>,
	    "forEachEntryWhile visitor must return exactly bool");
	for (std::uint32_t group = 0; group < count; ++group) {
		const auto& catalog = catalogs[group];
		for (std::uint32_t entry = 0; entry < catalog.count; ++entry)
			if (!std::invoke(visitor, (static_cast<PackedId>(group) << 16) | entry,
			                 std::string_view{catalog.name}, catalog.entries[entry]))
				return false;
	}
	return true;
}

// Runtime group thunks forwarding checked local entry selection.
// Public methods:
// - invoke(): Visit selected group.
// - make(): Build group branches.
template<class Tuple, class Visitor>
struct GroupDispatch {
	using Invoke = bool (*)(const Tuple&, std::uint32_t, Visitor&);

	template<std::size_t Group>
	static bool invoke(const Tuple& groups, std::uint32_t entry, Visitor& visitor)
	{
		return std::get<Group>(groups).table->visit(entry, visitor);
	}

	template<std::size_t... Group>
	static consteval auto make(std::index_sequence<Group...>) noexcept
	{
		return std::array<Invoke, sizeof...(Group)>{{&invoke<Group>...}};
	}

	// Two bounded index operations select a global endpoint: group, then row.
	// There is no flattened owning value or runtime type switch.
	inline static constexpr auto entries =
	    make(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

} // namespace telemetry::detail

#endif
