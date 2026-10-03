/*
 * @file NoHeapProbe.cpp
 * @brief Large codec path uses caller storage without dynamic allocation.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace {
bool denyAllocation = false;
int destructorCalls = 0;
using Big = std::array<std::uint32_t, 1024>;

struct Tracked {
    ~Tracked() { ++destructorCalls; }
};

void require(bool condition)
{
    if (!condition) std::abort();
}
} // namespace

void* operator new(std::size_t count)
{
    if (denyAllocation) std::abort();
    if (void* result = std::malloc(count)) return result;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t count)
{
    return ::operator new(count);
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main()
{
    using namespace telemetry;
    static Big source{};
    static std::array<std::byte, wireSize<Big>> wire{};
    alignas(Big) static std::array<std::byte, scratchBytes<Big> + sizeof(Tracked)> storage{};
    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<std::uint32_t>(i * 131u + 7u);
    source[0] = 0x12345678;
    source[1023] = 0xabcdef01;

    denyAllocation = true;
    require(encode(source, wire) == CodecStatus::Ok);
    require(wire[0] == std::byte{0x78});
    require(wire[4095] == std::byte{0xab});

    Workspace workspace{storage};
    {
        auto lease = workspace.reserve<Big>();
        Big* output = nullptr;
        require(decode<Big>(wire, lease, output) == CodecStatus::Ok);
        require(output != nullptr);
        for (std::size_t i = 0; i < source.size(); ++i)
            require((*output)[i] == source[i]);
    }
    require(workspace.used() == 0);

    {
        auto tracked = workspace.reserve<Tracked>();
        require(tracked.constructFrom([] { return Tracked{}; }) != nullptr);
    }
    require(destructorCalls == 1 && workspace.used() == 0);
    denyAllocation = false;
}
