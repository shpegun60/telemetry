/*
 * @file MixedFixture.hpp
 * @brief One mixed Field table and a type shared by all three endpoint families.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <telemetry/Telemetry.hpp>
#include <array>

namespace fixture {
namespace ts = telemetry;
enum class Mode : std::uint8_t { Off, Run, Fault };
enum class Position : std::uint8_t { Enabled, Rpm, Temperature, Precise, Mode, Samples, State, Config };
struct MotorConfig { float target; std::uint16_t rpm; bool enabled; };
struct State { float volts; bool active; };

struct Device {
    MotorConfig current{230.0f, 1500, true};
    int reads = 0;
    int writes = 0;
    int commands = 0;
    int services = 0;
    telemetry::WriteResult writeStatus = telemetry::WriteResult::Applied;
    telemetry::CommandResult commandStatus = telemetry::CommandResult::Executed;

    bool enabled() noexcept { ++reads; return current.enabled; }
    std::uint16_t rpm() noexcept { ++reads; return current.rpm; }
    float temperature() noexcept { ++reads; return 21.5f; }
    double precise() noexcept { ++reads; return 0.125; }
    Mode mode() noexcept { ++reads; return Mode::Run; }
    std::array<std::uint16_t, 3> samples() noexcept { ++reads; return {1, 256, 65535}; }
    State state() noexcept { ++reads; return {current.target, current.enabled}; }
    MotorConfig config() noexcept { ++reads; return current; }
    telemetry::WriteResult setConfig(const MotorConfig& value) noexcept
    { ++writes; current = value; return writeStatus; }
    telemetry::CommandResult configure(const MotorConfig& value) noexcept
    { ++commands; current = value; return commandStatus; }
    telemetry::CommandResult reset() noexcept
    { ++commands; return commandStatus; }
    MotorConfig echo(const MotorConfig& value) noexcept
    { ++services; return value; }
};
inline Device device;
inline constexpr ts::FieldTable mixedFields{
    ts::field<&Device::enabled>("Enabled", device),
    ts::field<&Device::rpm>("RPM", device),
    ts::field<&Device::temperature>("Temperature", device),
    ts::field<&Device::precise>("Precise", device),
    ts::field<&Device::mode>("Mode", device),
    ts::field<&Device::samples>("Samples", device),
    ts::field<&Device::state>("State", device),
    ts::field<&Device::config, &Device::setConfig>("Config", device)};
inline constexpr ts::CommandTable localCommands{
    ts::command<&Device::configure>("Configure", device),
    ts::command<&Device::reset>("Reset", device)};
inline constexpr ts::ServiceTable localServices{ts::service<&Device::echo>("Echo", device)};
inline constexpr ts::FieldCatalogTable fields{ts::group("motor", mixedFields)};
inline constexpr ts::CommandCatalogTable commands{ts::group("motor", localCommands)};
inline constexpr ts::ServiceCatalogTable services{ts::group("motor", localServices)};
inline constexpr ts::Model model{fields, commands, services};
}
