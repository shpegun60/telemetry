/*
 * @file Device.hpp
 * @brief Reflected DTOs and a fake device for the v3 client example.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <telemetry_structured/Structured.hpp>
#include <resource/structured/Descriptor.hpp>
#include <resource/structured/ValuesFile.hpp>
#include <array>
#include <limits>

namespace client_example {
namespace ts = telemetry::structured;
namespace rs = resource::structured;

enum class Mode : std::int16_t { Off = -1, On = 2 };

struct Config {
    std::uint64_t serial;
    std::int64_t offset;
    float gain;
    double precise;
    bool enabled;
    Mode mode;
    std::array<std::uint16_t, 3> samples;
};

struct Request { Config config; std::uint32_t multiplier; };
struct Response { Config config; std::uint32_t checksum; };

inline constexpr Config initial{
    UINT64_MAX, INT64_MIN, -0.0f, 0.125, true,
    static_cast<Mode>(-2), {1, 256, 65535}};
inline constexpr Request request{initial, 4};

struct Device {
    Config config = initial;
    unsigned calls = 0;

    Config readConfig() const noexcept { return config; }
    telemetry::WriteResult writeConfig(const Config& value) noexcept
    {
        ++calls;
        config = value;
        return telemetry::WriteResult::Applied;
    }
    telemetry::CommandResult apply(const Config& value) noexcept
    {
        ++calls;
        config = value;
        return telemetry::CommandResult::Executed;
    }
    telemetry::CommandResult reset() noexcept
    {
        ++calls;
        config = initial;
        return telemetry::CommandResult::Executed;
    }
    ts::ServiceResult<Response> inspect(const Request& value) noexcept
    {
        ++calls;
        // These are application decisions, not descriptor limits/defaults.
        switch (value.multiplier) {
        case 0: return ts::ServiceResult<Response>::failure(ts::ServiceStatus::InvalidArgument);
        case 1: return ts::ServiceResult<Response>::failure(ts::ServiceStatus::Busy);
        case 2: return ts::ServiceResult<Response>::failure(ts::ServiceStatus::Unavailable);
        case 3: return ts::ServiceResult<Response>::failure(ts::ServiceStatus::Failed);
        default: break;
        }
        std::uint32_t sum = 0;
        for (auto sample : value.config.samples) sum += sample;
        return ts::ServiceResult<Response>::success({value.config, sum * value.multiplier});
    }
    void ping() noexcept { ++calls; }
};

inline Device device;
inline telemetry::FunctionSlot<Response(const Request&) noexcept> offline;
inline constexpr ts::FieldTable localFields{
    ts::field<&Device::readConfig, &Device::writeConfig>("Config", device)};
inline constexpr ts::CommandTable localCommands{
    ts::command<&Device::apply>("Apply", device),
    ts::command<&Device::reset>("Reset", device)};
inline constexpr ts::ServiceTable localServices{
    ts::service<&Device::inspect>("Inspect", device),
    ts::service<&Device::ping>("Ping", device),
    ts::service("Offline", offline)};
inline constexpr ts::FieldCatalogTable fields{ts::group("device", localFields)};
inline constexpr ts::CommandCatalogTable commands{ts::group("device", localCommands)};
inline constexpr ts::ServiceCatalogTable services{ts::group("device", localServices)};
inline constexpr ts::Model model{fields, commands, services};
inline constexpr rs::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = rs::packDescriptor<descriptor>();

// The desktop fake owns its scratch explicitly, just like an MCU integrator.
inline std::array<std::byte, 2048> scratch;
inline ts::Workspace workspace{scratch};
inline constexpr rs::ValuesFile values{descriptor, workspace};
} // namespace client_example
