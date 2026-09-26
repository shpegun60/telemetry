/*
 * @file MagicEnumAdapter.hpp
 * @brief Isolate the pinned C++20 enum-name backend from the reflection API.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_MAGIC_ENUM_ADAPTER_HPP
#define TELEMETRY_STRUCTURED_MAGIC_ENUM_ADAPTER_HPP

#include <magic_enum.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace telemetry::structured::reflection::detail {

// The pinned backend can inspect one named value outside its automatic scan
// range. Inspect the raw result before calling its public helper: on an
// unnamed code, that helper produces a vendor diagnostic before ours.
template <auto Value>
consteval auto rawEnumName() noexcept
{
    return magic_enum::detail::n<Value>();
}

template <class E>
inline constexpr auto enumNamePrefixLength = magic_enum::detail::prefix_length_or_zero<E>;

template <auto Value>
consteval std::string_view backendEnumName() noexcept
{
    return magic_enum::enum_name<Value>();
}

template <class E>
consteval auto backendEnumEntries() noexcept
{
    constexpr auto source = magic_enum::enum_entries<E>();
    std::array<std::pair<E, std::string_view>, source.size()> result{};
    for (std::size_t i = 0; i < source.size(); ++i)
        result[i] = {source[i].first, source[i].second};
    return result;
}

} // namespace telemetry::structured::reflection::detail

#endif
