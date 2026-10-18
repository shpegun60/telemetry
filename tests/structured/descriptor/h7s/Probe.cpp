/*
 * @file Probe.cpp
 * @brief Separate-TU indexed emission and packed-byte copy controls.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "../Fixture.hpp"
#include <algorithm>
using ::telemetry::descriptor::ReadResult;
using ::telemetry::descriptor::Cursor;
using ::telemetry::descriptor::Output;
namespace df = descriptor_fixture;

extern "C" __attribute__((noinline)) ReadResult descriptor_stream(Cursor cursor,
                                                                  Output out) noexcept
{
	return df::edge.read(cursor, out);
}

extern "C" __attribute__((noinline)) ReadResult descriptor_packed(Cursor cursor,
                                                                  Output out) noexcept
{
	if (cursor > df::edgeBytes.size())
		return {::telemetry::descriptor::ReadStatus::InvalidCursor, cursor};
	if (cursor == df::edgeBytes.size())
		return {::telemetry::descriptor::ReadStatus::Ok, cursor, 0, true};
	if (out.empty())
		return {::telemetry::descriptor::ReadStatus::BufferTooSmall, cursor};
	const auto count =
	    std::min(out.size(), df::edgeBytes.size() - static_cast<std::size_t>(cursor));
	for (std::size_t i = 0; i < count; ++i)
		out[i] = df::edgeBytes[cursor + i];
	return {::telemetry::descriptor::ReadStatus::Ok, cursor + count,
	        static_cast<std::uint32_t>(count), cursor + count == df::edgeBytes.size()};
}
