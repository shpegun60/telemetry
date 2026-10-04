// Application business types; no tables, catalogs or Model templates here.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#pragma once

#include <telemetry/result/EndpointStatus.hpp>
#include <array>
#include <cstdint>
#include <optional>

namespace app {
enum class Mode : std::uint8_t { Off = 0, Measuring = 1 };
using Gains = std::array<std::uint16_t, 3>;
struct SamplingConfig { std::uint32_t periodMs; Mode mode; Gains gains; };
struct DisplayConfig { std::uint8_t brightness; bool enabled; };
struct Configure { SamplingConfig sampling; DisplayConfig display; };
struct Query { std::uint16_t channel; };
struct Snapshot {
    std::uint32_t changes;
    std::uint16_t gain;
    Mode mode;
    bool displayEnabled;
};
struct Counters { unsigned reads, writes, commands, services; };
using WriteStatus = telemetry::WriteResult;
using CommandStatus = telemetry::CommandResult;

class Device {
public:
    std::uint32_t readPeriod() const noexcept;
    Mode readMode() const noexcept;
    Gains readGains() const noexcept;
    DisplayConfig readDisplay() const noexcept;
    WriteStatus writePeriod(std::uint32_t value) noexcept;
    WriteStatus writeMode(Mode value) noexcept;
    WriteStatus writeGains(const Gains& value) noexcept;
    WriteStatus writeDisplay(const DisplayConfig& value) noexcept;
    CommandStatus reset() noexcept;
    CommandStatus configure(const Configure& request) noexcept;
    Snapshot sample() const noexcept;
    std::optional<Snapshot> query(const Query& request) const noexcept;
    Counters counters() const noexcept { return counters_; }

private:
    SamplingConfig sampling_{10, Mode::Measuring, {100, 100, 100}};
    DisplayConfig display_{50, true};
    std::uint32_t changes_ = 0;
    mutable Counters counters_{};
};
} // namespace app
