/* Bounded owning/borrowed H7S comparisons. Authors: Ruslan Kovtun
 * (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#pragma once
#include <telemetry/Telemetry.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace borrowed_h7s {
namespace ts = telemetry;
template <unsigned N> struct Blob { std::array<std::uint8_t, N> bytes; };
using Small = Blob<4096>;
using Large = Blob<65536>;
struct Request { std::uint32_t selection; };
using Probe = std::uint32_t (*)(std::uint32_t) noexcept;
struct Operation { const char* name; Probe invoke; unsigned bytes, iterations; bool encoded; };
struct Checks { unsigned checked = 0, failed = 0, payloadBytes = 0; };
inline constexpr unsigned sequenceSize = 128, operationCount = 14;
inline constexpr unsigned timingRepeats = 7, stackRepeats = 3;
inline constexpr unsigned expectedChecks = 127, expectedPayloadBytes = 626688;
inline constexpr unsigned scratchCapacity = ts::scratchBytes<ts::ServiceResult<Large>> + 64;
inline constexpr std::uint32_t maximumCallCycles = 100000000, maximumWindowCycles = 1000000000;
extern std::array<Small, 2> cache4;
extern std::array<Large, 2> cache64;
extern std::array<std::byte, 65536> output;
extern std::array<std::byte, scratchCapacity> scratch;
extern ts::Workspace workspace;
extern volatile std::uint32_t callbacks;
extern const std::array<Operation, operationCount> operations;
void prepare() noexcept;
Checks check() noexcept;
void sequence(unsigned profile, std::array<std::uint32_t, sequenceSize>& ids) noexcept;
std::uint32_t expected(unsigned bytes, unsigned selection) noexcept;
std::uint32_t expectedSum(unsigned bytes, unsigned count,
                         const std::array<std::uint32_t, sequenceSize>& ids) noexcept;
std::uint8_t pattern(unsigned position, unsigned selection) noexcept;
}
