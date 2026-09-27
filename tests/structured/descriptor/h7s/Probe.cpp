/*
 * @file Probe.cpp
 * @brief Separate-TU indexed emission and packed-byte copy controls.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "../Fixture.hpp"
#include <algorithm>
using resource::ReadResult;
using resource::Cursor;
using resource::Output;
namespace df = descriptor_fixture;
extern "C" __attribute__((noinline)) ReadResult descriptor_stream(Cursor cursor, Output out) noexcept
{ return df::edge.read(cursor, out); }
extern "C" __attribute__((noinline)) ReadResult descriptor_packed(Cursor cursor, Output out) noexcept
{
    if (cursor > df::edgeBytes.size()) return {resource::Status::InvalidCursor, cursor};
    if (cursor == df::edgeBytes.size()) return {resource::Status::Ok, cursor, 0, true};
    if (out.empty()) return {resource::Status::BufferTooSmall, cursor};
    const auto count = std::min(out.size(), df::edgeBytes.size() - static_cast<std::size_t>(cursor));
    for (std::size_t i = 0; i < count; ++i) out[i] = df::edgeBytes[cursor + i];
    return {resource::Status::Ok, cursor + count, static_cast<std::uint32_t>(count), cursor + count == df::edgeBytes.size()};
}
