/*
 * @file ProbeTypes.hpp
 * @brief Shared exact types for registry identity checks across translation units.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_REGISTRY_PROBE_TYPES_HPP
#define TELEMETRY_REGISTRY_PROBE_TYPES_HPP
#pragma once

#include <array>
#include <cstdint>

namespace registry_probe {

enum class Mode : std::int16_t {
	Below = -5,
	Normal = 0,
	Above = 5
};

// Native DTO whose exact identity is reused by registry roots and nested members.
struct Reading {
	float voltage;
	std::uint16_t status;
};

// Same layout as Reading but a distinct C++ type: never deduplicated by shape alone.
struct SameShape {
	float voltage;
	std::uint16_t status;
};

// Recursive registry root verifies postorder member, array and enum dependencies.
struct SampleBlock {
	Reading reading;
	std::array<std::uint16_t, 3> samples;
	Mode mode;
};

} // namespace registry_probe

#endif
