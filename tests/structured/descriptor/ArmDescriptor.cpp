/*
 * @file ArmDescriptor.cpp
 * @brief ARM golden extraction, immutable storage, bounded reader stack/size.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"
#include <algorithm>
namespace df = descriptor_fixture;
namespace rs = resource::structured;

#if defined(PACKED_ONLY)
inline constexpr auto image = df::edgeBytes;
extern "C" const void* descriptor_storage() noexcept { return image.data(); }
extern "C" __attribute__((noinline)) resource::ReadResult
descriptor_read(resource::Cursor cursor, std::span<std::byte> output) noexcept
{
    if (cursor > image.size()) return {resource::Status::InvalidCursor, cursor};
    if (cursor == image.size()) return {resource::Status::Ok, cursor, 0, true};
    if (output.empty()) return {resource::Status::BufferTooSmall, cursor};
    const auto count = std::min(output.size(), image.size() - static_cast<std::size_t>(cursor));
    for (std::size_t i = 0; i < count; ++i) output[i] = image[cursor + i];
    return {resource::Status::Ok, cursor + count, static_cast<resource::FileSize>(count), cursor + count == image.size()};
}
#else
extern "C" const void* descriptor_storage() noexcept { return &df::edge; }
extern "C" __attribute__((noinline)) resource::ReadResult
descriptor_read(resource::Cursor cursor, std::span<std::byte> output) noexcept
{ return df::edge.read(cursor, output); }
#endif

#if defined(EXTRACT_GOLDENS)
extern "C" {
__attribute__((used, section(".descriptor.mixed"))) const auto descriptor_mixed = df::mixedBytes;
__attribute__((used, section(".descriptor.edge"))) const auto descriptor_edge = df::edgeBytes;
__attribute__((used, section(".descriptor.empty"))) const auto descriptor_empty = df::emptyBytes;
}
#endif

// Export both the producer and storage root to prevent linker GC from making
// the packed-vs-streaming size comparison accidentally omit the index table.
extern "C" __attribute__((used)) std::uintptr_t descriptor_roots(std::uint32_t cursor) noexcept
{
    std::array<std::byte, 32> out;
    return descriptor_read(cursor, out).written + reinterpret_cast<std::uintptr_t>(descriptor_storage());
}
