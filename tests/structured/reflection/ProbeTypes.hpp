/*
 * @file ProbeTypes.hpp
 * @brief Externally linked aggregates shared by two reflection probe units.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_PROBE_TYPES_HPP
#define TELEMETRY_PROBE_TYPES_HPP
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace telemetry_structured_probe {

// External-linkage aggregate verifies member names/types agree across two TUs.
struct MeterConfig {
	float voltage;
	std::uint16_t rpm;
};

// Repeated leaf types retain distinct member names and declaration positions.
struct RepeatedTypes {
	float phaseA;
	float phaseB;
};

// Nested aggregate and fixed array expose recursive facade facts without construction.
struct Nested {
	MeterConfig config;
	std::array<std::uint16_t, 3> samples;
};

// This declaration deliberately tests the supported ASCII-only automatic names.
struct NonAsciiName {
	std::uint8_t café;
};

enum class Mode : std::uint16_t {
	Off,
	Auto,
	Manual
};
enum class SignedMode : std::int16_t {
	Below = -5,
	Zero = 0,
	Above = 5
};
enum class SparseMode : std::uint16_t {
	None = 0,
	Far = 1000
};
enum class Aliases : std::uint8_t {
	Ready = 1,
	Ok = 1
};

constexpr bool asciiIdentifier(std::string_view name) noexcept
{
	if (name.empty())
		return false;

	auto letter = [](unsigned char byte) constexpr noexcept {
		return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
	};
	auto digit = [](unsigned char byte) constexpr noexcept {
		return byte >= '0' && byte <= '9';
	};

	if (!letter(static_cast<unsigned char>(name.front())))
		return false;
	for (char c : name.substr(1)) {
		auto byte = static_cast<unsigned char>(c);
		if (!letter(byte) && !digit(byte))
			return false;
	}
	return true;
}

} // namespace telemetry_structured_probe

#endif
