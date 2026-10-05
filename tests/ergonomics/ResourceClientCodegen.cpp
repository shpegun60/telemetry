// Resource READ builder equivalence against explicit little-endian bytes.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#include <resource/protocol/Client.hpp>

namespace client = resource::protocol::client;

#if defined(RESOURCE_CLIENT_REFERENCE)
// This is the caller's previous explicit encoding, kept as an independent
// source-level reference. Both probes retain the same bounded result ABI.
static constexpr client::BuildResult
explicitRead(resource::Output output, resource::FileIndex index, resource::Cursor cursor) noexcept
{
	if (output.size() < 13) {
		return {client::BuildStatus::BufferTooSmall, 0};
	}
	output[0] = std::byte{3};
	for (std::size_t i = 0; i != 4; ++i) {
		output[1 + i] = std::byte((std::uint64_t(index) >> (8 * i)) & 0xffu);
	}
	for (std::size_t i = 0; i != 8; ++i) {
		output[5 + i] = std::byte((cursor >> (8 * i)) & 0xffu);
	}
	return {client::BuildStatus::Ok, 13};
}
#else
using client::makeRead;
#define explicitRead makeRead
#endif

extern "C" client::BuildResult resource_client_read(std::byte* output, std::size_t capacity,
                                                    resource::FileIndex index,
                                                    resource::Cursor cursor) noexcept
{
	return explicitRead({output, capacity}, index, cursor);
}

extern "C" client::BuildResult resource_client_read_fixed(std::byte* output,
                                                          resource::FileIndex index,
                                                          resource::Cursor cursor) noexcept
{
	return explicitRead({output, 13}, index, cursor);
}
