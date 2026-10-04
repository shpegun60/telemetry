/*
 * @file ProbeTypes.hpp
 * @brief Shared exact types for Stage 05 registry tests across translation units.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_REGISTRY_PROBE_TYPES_HPP
#define TELEMETRY_REGISTRY_PROBE_TYPES_HPP

#include <array>
#include <cstdint>

namespace registry_probe {

enum class Mode : std::int16_t { Below = -5, Normal = 0, Above = 5 };

struct Reading {
    float voltage;
    std::uint16_t status;
};

struct SameShape {
    float voltage;
    std::uint16_t status;
};

struct SampleBlock {
    Reading reading;
    std::array<std::uint16_t, 3> samples;
    Mode mode;
};

} // namespace registry_probe

#endif
