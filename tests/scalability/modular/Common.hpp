/*
 * Shared request DTOs for the modular composition fixture.
 *
 * These types are visible to private module schema headers. Runtime consumers
 * need only encoded bytes and never include this file.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#ifndef TELEMETRY_TESTS_SCALABILITY_MODULAR_COMMON_HPP
#define TELEMETRY_TESTS_SCALABILITY_MODULAR_COMMON_HPP
#pragma once

#include <cstdint>

namespace modular {
struct Delta {
	std::uint32_t amount;
};

struct Query {
	std::uint32_t factor;
};
} // namespace modular
#endif
