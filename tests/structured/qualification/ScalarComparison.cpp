/* Same scalar owner: direct/new/legacy typed paths and descriptor layouts.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#define ENDPOINT_LEGACY
#include "../endpoints/h7s/Fixture.hpp"
using namespace bench;
#define PROBE extern "C" __attribute__((noinline))
PROBE std::optional<std::uint32_t> scalar_direct() noexcept { return device.read(); }
PROBE std::optional<std::uint32_t> scalar_new() noexcept { return fieldSeed.read<0>(); }
PROBE std::optional<std::uint32_t> scalar_old() noexcept { return oldFieldSeed.read<0>(); }
PROBE telemetry::WriteResult write_direct(std::uint32_t value) noexcept { return device.write(value); }
PROBE telemetry::WriteResult write_new(std::uint32_t value) noexcept { return fieldSeed.write<0>(value); }
PROBE telemetry::WriteResult write_old(std::uint32_t value) noexcept { return oldFieldSeed.write<0>(value); }
extern "C" __attribute__((used, section(".rodata.qualification_layout")))
const std::uint32_t qualification_layout[]{
    sizeof(telemetry::Field), alignof(telemetry::Field),
    sizeof(ts::FieldEntry), alignof(ts::FieldEntry),
    sizeof(telemetry::Command), alignof(telemetry::Command),
    sizeof(ts::CommandEntry), alignof(ts::CommandEntry),
    sizeof(ts::ServiceEntry), alignof(ts::ServiceEntry)};
