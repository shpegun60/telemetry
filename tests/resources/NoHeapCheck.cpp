/* A fixed provider and protocol must not allocate while serving requests.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>

void* operator new(std::size_t) { std::abort(); }
void* operator new[](std::size_t) { std::abort(); }
void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, std::size_t) noexcept {}
void operator delete[](void*, std::size_t) noexcept {}

namespace {
struct Provider {
    resource::FileSize size() const noexcept { return 1; }
    resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept
    {
        if (cursor > 1) return {resource::Status::InvalidCursor, cursor};
        if (cursor == 1) return {resource::Status::Ok, cursor, 0, true};
        if (output.empty()) return {resource::Status::BufferTooSmall, cursor};
        output[0] = std::byte{42};
        return {resource::Status::Ok, 1, 1, true};
    }
} provider;
constexpr auto files = resource::filesystem(resource::file("/memory", provider));
unsigned checks;
void check(bool result) { ++checks; if (!result) std::abort(); }
}

int main()
{
    std::array<std::byte, 64> output{};
    const std::array<std::byte, 9> list{std::byte{1}};
    for (unsigned repeat = 0; repeat < 1000; ++repeat) {
        check(files.stat(0).size == 1);
        const auto read = files.read(0, 0, output);
        check(read.status == resource::Status::Ok && read.written == 1 && output[0] == std::byte{42});
        check(files.read(0, 1, output).eof);
        check(files.write(0, 0, {}).status == resource::Status::NotWritable);
        const auto response = resource_protocol::process(files.view(), list, output);
        check(response.status == resource::Status::Ok && response.written > 12);
    }
    std::printf("Generic resource without heap: %u checks passed\n", checks);
}
