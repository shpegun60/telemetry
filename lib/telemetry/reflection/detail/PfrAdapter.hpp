/*
 * @file PfrAdapter.hpp
 * @brief C++20 aggregate backend; vendor types stay behind this file.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_PFR_ADAPTER_HPP
#define TELEMETRY_STRUCTURED_PFR_ADAPTER_HPP

#include <boost/pfr/config.hpp>
#include <boost/pfr/core.hpp>
#include <boost/pfr/core_name.hpp>
#include <boost/pfr/tuple_size.hpp>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#if _MSVC_LANG < 202002L
#error "Structured telemetry requires C++20"
#endif
#elif __cplusplus < 202002L
#error "Structured telemetry requires C++20"
#endif

#if !BOOST_PFR_CORE_NAME_ENABLED
#error "Structured telemetry requires C++20 and Boost.PFR member names"
#endif

namespace telemetry::reflection::detail {

template <class T>
using ReflectedType = std::remove_cv_t<std::remove_reference_t<T>>;

template <class T>
inline constexpr std::size_t pfrMemberCount = boost::pfr::tuple_size_v<ReflectedType<T>>;

template <std::size_t I, class T>
using PfrMemberType = boost::pfr::tuple_element_t<I, ReflectedType<T>>;

template <std::size_t I, class T>
constexpr std::string_view pfrMemberName() noexcept
{
    return boost::pfr::get_name<I, ReflectedType<T>>();
}

template <std::size_t I, class T>
constexpr decltype(auto) pfrGet(T&& object)
    noexcept(noexcept(boost::pfr::get<I>(std::forward<T>(object))))
{
    return boost::pfr::get<I>(std::forward<T>(object));
}

} // namespace telemetry::reflection::detail

#endif
