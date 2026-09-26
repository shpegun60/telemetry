// Critic trial consumer: a small metering device described only from the READMEs.
// Written as a first-time adopter would; not part of the library or its checks.
#pragma once

#include "Telemetry.h"

#include <cstdint>

namespace critic {

using telemetry::CommandResult;
using telemetry::WriteResult;

enum class Mode : std::uint8_t { Off, Auto, Manual };

struct Device {
    float ua = 230.1f;
    float ia = 1.5f;
    double tempC = 31.5;
    std::uint32_t uptime = 0;
    float overVoltageLimit = 260.0f;
    Mode mode_ = Mode::Auto;
    std::uint32_t resets = 0;

    float voltage() const noexcept { return ua; }
    float current() const noexcept { return ia; }
    float power() const noexcept { return ua * ia; }
    double temperature() const noexcept { return tempC; }
    std::uint32_t uptimeSeconds() const noexcept { return uptime; }
    float limit() const noexcept { return overVoltageLimit; }
    WriteResult setLimit(float v) noexcept
    {
        overVoltageLimit = v;
        return WriteResult::Applied;
    }
    Mode mode() const noexcept { return mode_; }
    WriteResult setMode(Mode m) noexcept
    {
        mode_ = m;
        return WriteResult::Applied;
    }
    CommandResult reset() noexcept
    {
        ++resets;
        uptime = 0;
        return CommandResult::Executed;
    }
    CommandResult configure(float limitVolts, Mode m) noexcept
    {
        overVoltageLimit = limitVolts;
        mode_ = m;
        return CommandResult::Executed;
    }
};

inline Device device;

inline constexpr telemetry::FieldTable meterFields{
    telemetry::field<&Device::voltage>("Ua", "V", device),
    telemetry::field<&Device::current>("Ia", "A", device),
    telemetry::field<&Device::power>("P", "W", device),
    telemetry::field<&Device::temperature>("Temperature", "degC", device),
    telemetry::field<&Device::uptimeSeconds>("Uptime", "s", device),
    telemetry::field<&Device::limit, &Device::setLimit>("OverVoltage", "V", device,
        telemetry::limits(260.0f, 100.0f, 300.0f))
        .withFlags(telemetry::FieldFlag::Persistent),
    telemetry::field<&Device::mode, &Device::setMode>("Mode", "", device,
        telemetry::limits(Mode::Auto))
        .withFlags(telemetry::FieldFlag::Persistent),
};

inline constexpr telemetry::CommandTable meterCommands{
    telemetry::command<&Device::reset>("Reset", device),
    telemetry::command<&Device::configure>("Configure", device,
        telemetry::arg<0>("Limit", "V", 260.0f, 100.0f, 300.0f),
        telemetry::arg<1>("Mode", "", Mode::Auto)),
};

inline constexpr telemetry::FieldCatalogTable fields{telemetry::group("meter", meterFields)};
inline constexpr telemetry::CommandCatalogTable commands{telemetry::group("meter", meterCommands)};
inline constexpr auto fieldIndex = fields.index();
inline constexpr auto commandIndex = commands.index();

// Implemented in CriticResources.cpp (C++20) when the resource part is linked.
std::size_t dumpResources(char* text, std::size_t capacity) noexcept;
int saveResources(const char* directory) noexcept;

} // namespace critic
