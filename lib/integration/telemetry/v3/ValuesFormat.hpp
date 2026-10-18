/*
 * @file ValuesFormat.hpp
 * @brief Live Values wire constants owned by the Resource integration.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_V3_VALUES_FORMAT_HPP
#define TELEMETRY_INTEGRATION_V3_VALUES_FORMAT_HPP
#pragma once

#include <cstdint>

namespace resource::telemetry::v3 {

// Values carry a cached descriptor fingerprint, never a hash of live data.
inline constexpr std::uint16_t valuesHeaderBytes = 24;
enum class ValueStatus : std::uint8_t {
	Ok = 0,
	Unavailable = 1
};

} // namespace resource::telemetry::v3
#endif
