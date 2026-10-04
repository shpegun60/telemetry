/*
 * @file Aggregate.hpp
 * @brief Stable aggregate-reflection facade with exact member access.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_AGGREGATE_HPP
#define TELEMETRY_AGGREGATE_HPP

#include "detail/PfrAdapter.hpp"

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace telemetry::reflection {

// Automatic compiler-derived names are deliberately more restricted than
// explicit endpoint/group labels: they must be portable ASCII identifiers.
constexpr bool automaticNameValid(std::string_view name) noexcept
{
    const auto letter = [](unsigned char c) constexpr noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    const auto digit = [](unsigned char c) constexpr noexcept {
        return c >= '0' && c <= '9';
    };

    if (name.empty() || !letter(static_cast<unsigned char>(name.front()))) return false;
    for (char c : name.substr(1)) {
        const auto byte = static_cast<unsigned char>(c);
        if (!letter(byte) && !digit(byte)) return false;
    }
    return true;
}

template <class T>
inline constexpr std::size_t memberCount = detail::pfrMemberCount<T>;

template <std::size_t I, class T>
using MemberType = detail::PfrMemberType<I, T>;

template <std::size_t I, class T>
constexpr std::string_view memberName() noexcept
{
    constexpr auto name = detail::pfrMemberName<I, T>();
    static_assert(automaticNameValid(name),
                  "Automatic reflected member names must be ASCII identifiers");
    return name;
}

template <std::size_t I, class T>
constexpr decltype(auto) get(T&& object)
    noexcept(noexcept(detail::pfrGet<I>(std::forward<T>(object))))
{
    // PFR copies members from rvalue aggregates, so the facade accepts only
    // lvalues. Codec access then preserves cv/ref without a hidden copy.
    static_assert(std::is_lvalue_reference_v<T&&>,
                  "reflection::get requires an lvalue aggregate");
    return detail::pfrGet<I>(std::forward<T>(object));
}

} // namespace telemetry::reflection

#endif
