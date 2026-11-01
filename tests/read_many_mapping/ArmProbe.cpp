/*
 * @file ArmProbe.cpp
 * @brief Selected offline ARM roots for experimental compact and padded Service mappings.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "../../examples/read_many_mapping/Fixture.hpp"
#include <integration/encoded/Dispatcher.hpp>

namespace rm = read_many_mapping;
namespace fx = rm::fixture;

namespace {
// These are explicit application buffers, not allocations inside the executor.
rm::Batch results;
#if defined(READ_MANY_PROBE_COMPACT)
std::array<std::byte, 32> storage{};
#elif defined(READ_MANY_PROBE_SERVICE)
std::array<std::byte, fx::serviceModel.maxScratch() + 32> storage{};
#else
#error Select one mapping root for the offline image.
#endif
} // namespace

#if defined(READ_MANY_PROBE_COMPACT)
extern "C" std::size_t read_many_compact_probe(const std::byte* request, std::size_t requestSize,
                                               std::byte* reply, std::size_t replySize) noexcept
{
	telemetry::Workspace workspace{storage};
	constexpr rm::Capability admitted{1, fx::scalarDescriptor.fingerprint()};
	return rm::dispatch(fx::scalarModel.view(), workspace, admitted, {request, requestSize},
	                    {reply, replySize}, results)
	    .written;
}
#endif
#if defined(READ_MANY_PROBE_SERVICE)
extern "C" std::size_t read_many_service_probe(const std::byte* request, std::size_t requestSize,
                                               std::byte* reply, std::size_t replySize) noexcept
{
	telemetry::Workspace workspace{storage};
	fx::serviceOwner.fields = fx::scalarModel.view();
	fx::serviceOwner.workspace = &workspace;
	fx::serviceOwner.batch = &results;
	const auto result = telemetry::integration::encoded::dispatchEncoded(
	    fx::serviceModel.view(), workspace, 1, {request, requestSize}, {reply, replySize});
	fx::serviceOwner.workspace = nullptr;
	fx::serviceOwner.batch = nullptr;
	return result.written;
}
#endif
