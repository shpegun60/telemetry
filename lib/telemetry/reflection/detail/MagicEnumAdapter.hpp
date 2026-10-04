/*
 * @file MagicEnumAdapter.hpp
 * @brief Isolate the pinned C++20 enum-name backend from the reflection API.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Contains all direct dependency on the pinned enum-name backend.
 *
 * The public reflection facade validates and normalizes the returned names.
 * Raw-name inspection gives the library a useful diagnostic for unnamed explicit
 * codes, while the translated entry array keeps backend implementation types
 * out of downstream descriptors.
 */

#ifndef TELEMETRY_MAGIC_ENUM_ADAPTER_HPP
#define TELEMETRY_MAGIC_ENUM_ADAPTER_HPP
#pragma once

#include <magic_enum.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace telemetry::reflection::detail {

// The pinned backend can inspect one named value outside its automatic scan
// range. Inspect the raw result before calling its public helper: on an
// unnamed code, that helper produces a vendor diagnostic before ours.
template<auto Value>
consteval auto rawEnumName() noexcept
{
	return magic_enum::detail::n<Value>();
}

template<class E>
inline constexpr auto enumNamePrefixLength = magic_enum::detail::prefix_length_or_zero<E>;

template<auto Value>
consteval std::string_view backendEnumName() noexcept
{
	return magic_enum::enum_name<Value>();
}

template<class E>
consteval auto backendEnumEntries() noexcept
{
	constexpr auto source = magic_enum::enum_entries<E>();
	std::array<std::pair<E, std::string_view>, source.size()> result{};
	for (std::size_t i = 0; i < source.size(); ++i)
		result[i] = {source[i].first, source[i].second};
	return result;
}

} // namespace telemetry::reflection::detail

#endif
