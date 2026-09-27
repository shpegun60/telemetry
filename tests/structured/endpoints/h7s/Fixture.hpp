/*
 * @file Fixture.hpp
 * @brief Identical endpoint definitions for cache-layout measurements.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <telemetry_structured/model/Model.hpp>
#ifdef ENDPOINT_LEGACY
#include <telemetry/Telemetry.h>
#endif
namespace bench {
namespace ts = telemetry::structured;
struct Request { std::uint32_t value; };
struct Device {
    std::uint32_t value = 17;
    std::uint32_t read() const noexcept { return value; }
    telemetry::WriteResult write(std::uint32_t next) noexcept
    { value = next; return telemetry::WriteResult::Applied; }
    telemetry::CommandResult command(const Request& next) noexcept
    { value = next.value; return telemetry::CommandResult::Executed; }
    Request service(const Request& input) const noexcept { return {value + input.value}; }
#ifdef ENDPOINT_LEGACY
    telemetry::CommandResult scalarCommand(std::uint32_t next) noexcept
    { value = next; return telemetry::CommandResult::Executed; }
#endif
};
inline Device device;
inline constexpr auto field = ts::field<&Device::read, &Device::write>("value", device);
inline constexpr auto command = ts::command<&Device::command>("set", device);
inline constexpr auto service = ts::service<&Device::service>("sum", device);
inline constexpr ts::FieldTable fieldSeed{field};
inline constexpr ts::CommandTable commandSeed{command};
inline constexpr ts::ServiceTable serviceSeed{service};

template <class Entry, std::size_t Count>
constexpr auto repeat(Entry entry)
{
    std::array<Entry, Count> result{};
    for (auto& value : result) value = entry;
    return result;
}
inline constexpr std::size_t small = 64, large = 1024;
alignas(32) inline constexpr auto flashFields = repeat<ts::FieldEntry, small>(fieldSeed.data()[0]);
alignas(32) inline constexpr auto flashCommands = repeat<ts::CommandEntry, small>(commandSeed.data()[0]);
alignas(32) inline constexpr auto flashServices = repeat<ts::ServiceEntry, small>(serviceSeed.data()[0]);

// The function pointer is selected outside the measured window. Per-call
// arguments remain runtime values in a separate TU without LTO.
using Probe = std::uint32_t (*)(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t read(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t write(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t execute(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t call(const void*, std::uint32_t, ts::Workspace&) noexcept;
extern std::array<std::byte, 4> input;

#ifdef ENDPOINT_COMPONENTS
std::uint32_t componentLocal(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentFind(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentPreflight(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentReserveScalar(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentReserveStruct(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentDecode(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentDecodeStruct(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentEncode(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentCallback(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t componentOverlap(const void*, std::uint32_t, ts::Workspace&) noexcept;
#endif

#ifdef ENDPOINT_LEGACY
inline constexpr telemetry::FieldTable oldFieldSeed{
    telemetry::field<&Device::read, &Device::write>("value", "", device)};
inline constexpr telemetry::CommandTable oldCommandSeed{
    telemetry::command<&Device::scalarCommand>("set", device)};
template <class T, std::size_t... I>
constexpr auto copies(const T& row, std::index_sequence<I...>)
{
    return std::array<T, sizeof...(I)>{{(static_cast<void>(I), row)...}};
}
alignas(32) inline constexpr auto oldFlashFields = copies(oldFieldSeed.data()[0], std::make_index_sequence<small>{});
alignas(32) inline constexpr auto oldFlashCommands = copies(oldCommandSeed.data()[0], std::make_index_sequence<small>{});
std::uint32_t oldRead(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t oldWrite(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t oldExecute(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t oldTypedRead(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t newTypedRead(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t oldTypedWrite(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t newTypedWrite(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t oldTypedCall(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t newTypedCall(const void*, std::uint32_t, ts::Workspace&) noexcept;
#endif

#ifdef ENDPOINT_STORAGE
// Resolved-entry probes retain the complete encoded boundary, but receive
// an entry selected outside the DWT window. Size probes use one-entry catalogs.
std::uint32_t resolvedRead(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t resolvedWrite(const void*, std::uint32_t, ts::Workspace&) noexcept;
std::uint32_t resolvedCommand(const void*, std::uint32_t, ts::Workspace&) noexcept;
extern const void* const sizeIndexes[12];
extern const Probe sizeProbes[12];
#endif
}
