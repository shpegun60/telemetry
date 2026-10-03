/*
 * @file Fixture.hpp
 * @brief Shared host/MCU probes; no device access in this header.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#pragma once

#include "../qualification/Fixture.hpp"
#include <span>

namespace mcu {
namespace ts = telemetry::structured;
namespace rs = resource::structured;

inline constexpr unsigned rows = 128;
inline constexpr unsigned repeats = 7;
inline constexpr unsigned sequenceSize = 128;
using Probe = std::uint32_t (*)(std::uint32_t) noexcept;

// Every timed call has the same narrow function-pointer ABI. Fixed-position
// probes run only the same-ID profile; runtime selectors also run sequential
// and shuffled IDs. Counts include the common loop and checksum accumulation.
struct Operation {
    const char* name;
    Probe invoke;
    unsigned profiles;
    unsigned iterations;
};

std::span<const Operation> operations() noexcept;
void prepare() noexcept;
std::uint32_t expected(unsigned operation, std::uint32_t id) noexcept;
void checkProbes() noexcept;

// Pure, reproducible input generation can be shared by host checks and a
// later MCU runner without depending on a library random-number generator.
inline void sequence(unsigned profile, std::span<std::uint32_t, sequenceSize> ids) noexcept
{
    for (unsigned i = 0; i < ids.size(); ++i) ids[i] = profile == 0 ? 0 : i;
    if (profile == 2) {
        std::uint32_t state = 0x51d724bu;
        for (unsigned i = sequenceSize - 1; i != 0; --i) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            const unsigned other = state % (i + 1);
            const auto value = ids[i];
            ids[i] = ids[other];
            ids[other] = value;
        }
    }
}

inline void checkWindow(unsigned operation, unsigned profile) noexcept
{
    // Input IDs are explicit test storage, outside the endpoint call chain.
    static std::array<std::uint32_t, sequenceSize> ids;
    sequence(profile, ids);
    const auto probe = operations()[operation].invoke;
    for (auto id : ids) qualification::check(probe(id) == expected(operation, id));
}
} // namespace mcu
