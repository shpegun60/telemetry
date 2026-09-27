/*
 * @file Probe.cpp
 * @brief Runtime lookup plus actual encoded endpoint dispatch, no LTO.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"
#include <cstring>
namespace bench {
namespace {
std::uint32_t word(const std::array<std::byte, 4>& bytes) noexcept
{
    std::uint32_t value;
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}
}
__attribute__((noinline))
std::uint32_t read(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    auto& index = *static_cast<const ts::FieldIndex*>(raw);
    std::array<std::byte, 4> output{};
    const auto result = index.readEncoded(id, output, workspace);
    asm volatile("" : "+m"(output) : : "memory");
    return result.dispatch == ts::DispatchStatus::Ok ? word(output) + result.written : 0;
}
__attribute__((noinline))
std::uint32_t write(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    auto& index = *static_cast<const ts::FieldIndex*>(raw);
    const auto result = index.writeEncoded(id, input, workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == telemetry::WriteResult::Applied;
}
__attribute__((noinline))
std::uint32_t execute(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    auto& index = *static_cast<const ts::CommandIndex*>(raw);
    const auto result = index.executeEncoded(id, input, workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == telemetry::CommandResult::Executed;
}
__attribute__((noinline))
std::uint32_t call(const void* raw, std::uint32_t id, ts::Workspace& workspace) noexcept
{
    auto& index = *static_cast<const ts::ServiceIndex*>(raw);
    std::array<std::byte, 4> output{};
    const auto result = index.callEncoded(id, input, output, workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == ts::ServiceStatus::Ok
        ? word(output) + result.written : 0;
}
#ifdef ENDPOINT_LEGACY
// This scalar compatibility adapter uses the same 4-byte LE value at the
// boundary. It needs no Workspace because legacy scalar values fit locally.
__attribute__((noinline))
std::uint32_t oldRead(const void* raw, std::uint32_t id, ts::Workspace&) noexcept
{
    const auto& index = *static_cast<const telemetry::CatalogIndex*>(raw);
    const auto value = index.read<std::uint32_t>(id);
    if (!value) return 0;
    std::array<std::byte, 4> output{};
    if (ts::encode(*value, output) != ts::CodecStatus::Ok) return 0;
    // Retain the actual encoded bytes just as in the structured read probe.
    asm volatile("" : "+m"(output) : : "memory");
    return word(output) + 4;
}
__attribute__((noinline))
std::uint32_t oldWrite(const void* raw, std::uint32_t id, ts::Workspace&) noexcept
{
    const auto& index = *static_cast<const telemetry::CatalogIndex*>(raw);
    // H7 is little endian; memcpy is exactly the four-byte LE decode here.
    return index.write(id, word(input)) == telemetry::WriteResult::Applied;
}
__attribute__((noinline))
std::uint32_t oldExecute(const void* raw, std::uint32_t id, ts::Workspace&) noexcept
{
    const auto& index = *static_cast<const telemetry::CommandCatalogIndex*>(raw);
    const auto argument = telemetry::Scalar::from(word(input));
    return index.execute(id, &argument, 1) == telemetry::CommandResult::Executed;
}
__attribute__((noinline))
std::uint32_t oldTypedRead(const void*, std::uint32_t, ts::Workspace&) noexcept
{ return oldFieldSeed.read<0>().value_or(0); }
__attribute__((noinline))
std::uint32_t newTypedRead(const void*, std::uint32_t, ts::Workspace&) noexcept
{ return fieldSeed.read<0>().value_or(0); }
__attribute__((noinline))
std::uint32_t oldTypedWrite(const void*, std::uint32_t, ts::Workspace&) noexcept
{ return oldFieldSeed.write<0>(word(input)) == telemetry::WriteResult::Applied; }
__attribute__((noinline))
std::uint32_t newTypedWrite(const void*, std::uint32_t, ts::Workspace&) noexcept
{ return fieldSeed.write<0>(word(input)) == telemetry::WriteResult::Applied; }
__attribute__((noinline))
std::uint32_t oldTypedCall(const void*, std::uint32_t, ts::Workspace&) noexcept
{ return oldCommandSeed.call<0>(word(input)) == telemetry::CommandResult::Executed; }
__attribute__((noinline))
std::uint32_t newTypedCall(const void*, std::uint32_t, ts::Workspace&) noexcept
{ return commandSeed.call<0>(Request{word(input)}) == telemetry::CommandResult::Executed; }
#endif
}
