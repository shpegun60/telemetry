/*
 * @file CoreOnly.cpp
 * @brief Descriptor consumer whose complete dependency closure excludes Resource.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/descriptor/Descriptor.hpp>
#include "../structured/endpoints/MixedFixture.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

namespace td = telemetry::descriptor;
inline constexpr td::Descriptor descriptor{fixture::model};
inline constexpr auto bytes = td::packDescriptor<descriptor>();
static_assert(descriptor.valid() && bytes.size() >= td::descriptorHeaderBytes);
static_assert(bytes[0] == std::byte{'T'} && bytes[3] == std::byte{'3'});

int main()
{
	std::size_t checks = 0;
	std::array<std::byte, 67> output{};
	for (std::size_t offset = 0; offset <= bytes.size(); ++offset) {
		for (std::size_t chunk : {0u, 1u, 2u, 7u, 16u, 67u}) {
			output.fill(std::byte{0xa5});
			const auto result = descriptor.read(offset, std::span{output}.first(chunk));
			const auto count = std::min(chunk, bytes.size() - offset);
			assert(result.status == (chunk == 0 && offset != bytes.size()
			                             ? td::ReadStatus::BufferTooSmall
			                             : td::ReadStatus::Ok));
			assert(result.next == offset + count && result.written == count);
			assert(result.eof == (offset + count == bytes.size()));
			assert(std::equal(output.begin(), output.begin() + count, bytes.begin() + offset));
			assert(std::all_of(output.begin() + count, output.end(), [](auto value) {
				return value == std::byte{0xa5};
			}));
			++checks;
		}
	}
	const auto view = descriptor.view();
	assert(view.fingerprint() == descriptor.fingerprint() && view.size() == bytes.size());
	assert(view.read(std::numeric_limits<td::Cursor>::max(), {}).status ==
	       td::ReadStatus::InvalidCursor);
	assert(fixture::device.reads == 0 && fixture::device.commands == 0 &&
	       fixture::device.services == 0);
	std::printf("Resource-free descriptor: %zu slice checks passed\n", checks);
}
