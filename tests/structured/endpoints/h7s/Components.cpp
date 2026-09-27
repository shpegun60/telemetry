/*
 * @file Components.cpp
 * @brief Isolated costs, measured by the same DWT window as complete calls.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"
#include <cstring>

namespace bench {

// The compiler barriers retain each local/leased object. All probes use the
// same indirect-call loop. Report their absolute times; differences are only
// estimates, since code placement and register allocation also contribute.
__attribute__((noinline))
std::uint32_t componentLocal(const void*, std::uint32_t, ts::Workspace&) noexcept
{
    std::uint32_t value;
    std::memcpy(&value, input.data(), sizeof(value));
    asm volatile("" : "+m"(value) : : "memory");
    return value;
}

__attribute__((noinline))
std::uint32_t componentFind(const void* raw, std::uint32_t id, ts::Workspace&) noexcept
{
    const auto* row = static_cast<const ts::FieldIndex*>(raw)->find(id);
    return row == nullptr ? 0 : row->wireBytes;
}

__attribute__((noinline))
std::uint32_t componentPreflight(const void*, std::uint32_t, ts::Workspace& workspace) noexcept
{
    return ts::buffersDisjoint(input, {}, workspace.storage());
}

__attribute__((noinline))
std::uint32_t componentReserveScalar(const void*, std::uint32_t, ts::Workspace& workspace) noexcept
{
    auto lease = workspace.reserve<std::uint32_t>();
    if (!lease.valid()) return 0;
    auto* value = lease.constructDefault();
    std::memcpy(value, input.data(), sizeof(*value));
    asm volatile("" : "+m"(*value) : : "memory");
    return *value;
}

__attribute__((noinline))
std::uint32_t componentReserveStruct(const void*, std::uint32_t, ts::Workspace& workspace) noexcept
{
    auto lease = workspace.reserve<Request>();
    if (!lease.valid()) return 0;
    auto* request = lease.constructDefault();
    std::memcpy(&request->value, input.data(), sizeof(request->value));
    asm volatile("" : "+m"(*request) : : "memory");
    return request->value;
}

__attribute__((noinline))
std::uint32_t componentDecode(const void*, std::uint32_t, ts::Workspace&) noexcept
{
    ts::codec_detail::Reader reader{input};
    return reader.integer<std::uint32_t>();
}

__attribute__((noinline))
std::uint32_t componentDecodeStruct(const void*, std::uint32_t, ts::Workspace& workspace) noexcept
{
    auto lease = workspace.reserve<Request>();
    if (!lease.valid()) return 0;
    const auto* request = ts::detail::decodeEndpoint<Request>(input, lease);
    return request == nullptr ? 0 : request->value;
}

__attribute__((noinline))
std::uint32_t componentEncode(const void*, std::uint32_t, ts::Workspace&) noexcept
{
    std::uint32_t value;
    std::memcpy(&value, input.data(), sizeof(value));
    std::array<std::byte, 4> output;
    ts::detail::encodeEndpoint(value, output);
    asm volatile("" : "+m"(output) : : "memory");
    std::memcpy(&value, output.data(), sizeof(value));
    return value;
}

__attribute__((noinline))
std::uint32_t componentCallback(const void*, std::uint32_t, ts::Workspace&) noexcept
{
    return field.read().value_or(0);
}

__attribute__((noinline))
std::uint32_t componentOverlap(const void*, std::uint32_t, ts::Workspace& workspace) noexcept
{
    return !ts::buffersOverlap(input, workspace.storage());
}

} // namespace bench
