/*
 * @file ResultNegative.cpp
 * @brief A large convenience response would create hidden stack storage.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/result/ServiceResult.hpp>

#include <array>
#include <cstdint>

using Big = std::array<std::uint32_t, 1024>;

auto invalidResult()
{
    return telemetry::ServiceResult<Big>::success(Big{});
}
