/* Instantiate the optional mixed batch API on Cortex-M7 without a native Model. MIT. */
#include <integration/batch/Client.hpp>
#include <integration/encoded/Backend.hpp>
namespace bt = telemetry::integration::batch;
namespace en = telemetry::integration::encoded;
using Client = telemetry::integration::dynamic::DynamicClient<4, 1024, 1536>;

static void batchCompletion(const bt::BatchResult&) noexcept
{}

telemetry::client::Submission batchClientProbe(Client& client, en::EncodedBackend& backend,
                                               bt::wire::Capability capability,
                                               std::span<const bt::Endpoint> endpoints,
                                               std::span<const telemetry::client::Input> payloads,
                                               telemetry::client::Output requestScratch) noexcept
{
	const auto binding = bt::bindBatch(client, backend, capability, endpoints);
	if (!binding)
		return {binding.status, {}};
	return bt::batch(client, binding.binding, payloads, requestScratch, batchCompletion);
}
