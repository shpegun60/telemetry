/*
 * @file Traversal.hpp
 * @brief Typed traversal of borrowed definitions, without owning value storage.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Typed visitation of heterogeneous definitions and grouped local tables.
 *
 * Visitors and definitions are borrowed, callback results are discarded and
 * no endpoint value is materialized. Ordered folds implement complete scans;
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
#include <utility>

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
