/* Offline selected typed Batch root: caller owns client/backend and transport. MIT. */
#include "Contracts.hpp"
#include <integration/batch/StaticClient.hpp>
#include <integration/encoded/Backend.hpp>
namespace api = static_batch_api;
namespace bt = telemetry::integration::batch;
namespace cl = telemetry::client;
using Data = api::control::types::Config;
using Selection =
    decltype(bt::items(bt::write(api::control::fields::Config, std::declval<Data&>()),
                       bt::read(api::control::fields::Config),
                       bt::command(api::control::commands::Configure, std::declval<Data&>()),
                       bt::service(api::control::services::Echo, std::declval<Data&>())));
template<class C>
using Client = cl::DeviceClient<C, 2, 256>;
template<class C>
using Store = bt::StaticBatchStorage<C, Selection>;
static_assert(std::is_trivially_destructible_v<Store<api::client::CodecPolicy>>);
constinit Store<api::client::CodecPolicy> generatedStore;
constinit Store<static_batch_contracts::Codec> handwrittenStore;
static volatile std::uint64_t observed = 0;

template<class C>
void qualify(Client<C>& client, telemetry::integration::encoded::EncodedBackend& backend,
             telemetry::protocol::encoded::batch::Capability capability, Data& argument,
             Store<C>& storage, std::uint32_t now) noexcept
{
	const auto request = bt::items(bt::write(api::control::fields::Config, argument),
	                               bt::read(api::control::fields::Config),
	                               bt::command(api::control::commands::Configure, argument),
	                               bt::service(api::control::services::Echo, argument));
	const auto bound = bt::bindBatch(client, backend, capability, request);
	if (bound)
		(void)bt::batch(client, bound.binding, request, storage, [](const auto& result) noexcept {
			const auto value = result.template get<3>();
			if (value && value->hasValue())
				observed = value->value().large;
		});
	client.process(now);
	client.close();
	client.process(now);
}

extern "C" void
static_batch_entry(Client<api::client::CodecPolicy>& generated,
                   telemetry::integration::encoded::EncodedBackend& generatedBackend,
                   Client<static_batch_contracts::Codec>& handwritten,
                   telemetry::integration::encoded::EncodedBackend& handwrittenBackend,
                   telemetry::protocol::encoded::batch::Capability capability, Data& argument,
                   std::uint32_t now) noexcept
{
	qualify(generated, generatedBackend, capability, argument, generatedStore, now);
	qualify(handwritten, handwrittenBackend, capability, argument, handwrittenStore, now);
}
