/* Native leaf endpoints versus direct calls after the namespace migration.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include <telemetry/Telemetry.hpp>

namespace {
struct Device {
    std::uint32_t value = 17;
    std::uint32_t read() const noexcept { return value; }
    telemetry::WriteResult write(std::uint32_t next) noexcept
    { value = next; return telemetry::WriteResult::Applied; }
};
Device device;
constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::read, &Device::write>("value", device)};
}

#define PROBE extern "C" __attribute__((noinline))
PROBE std::optional<std::uint32_t> leaf_direct() noexcept { return device.read(); }
PROBE std::optional<std::uint32_t> leaf_native() noexcept { return fields.read<0>(); }
PROBE telemetry::WriteResult write_direct(std::uint32_t value) noexcept { return device.write(value); }
PROBE telemetry::WriteResult write_native(std::uint32_t value) noexcept { return fields.write<0>(value); }
extern "C" __attribute__((used, section(".rodata.qualification_layout")))
const std::uint32_t qualification_layout[]{
    sizeof(telemetry::FieldEntry), alignof(telemetry::FieldEntry),
    sizeof(telemetry::CommandEntry), alignof(telemetry::CommandEntry),
    sizeof(telemetry::ServiceEntry), alignof(telemetry::ServiceEntry)};
