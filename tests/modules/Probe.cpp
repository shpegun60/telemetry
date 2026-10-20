/* Installed module consumer; no source-tree includes. Authors: shpegun60, codexAi. MIT. */
#include <array>
#include <cstddef>
#include <cstdint>

#if defined(MODULE_CORE) || defined(MODULE_RESOURCE_ADAPTER) || defined(MODULE_ENCODED_SERVER) ||  \
    defined(MODULE_RPC_SERVER) || defined(MODULE_RPC_SESSIONS)
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
static constexpr telemetry::ServiceCatalogTable services{};
static constexpr telemetry::Model model{telemetry::emptyFields, telemetry::emptyCommands, services};
#endif
#if defined(MODULE_CORE)
#include <telemetry/encoded/Dispatcher.hpp>

int main()
{
	std::array<std::byte, 256> scratch{};
	telemetry::Workspace workspace{scratch};
	const auto result = telemetry::dispatchEncoded(model.view(), telemetry::Operation::FieldRead, 0,
	                                               {}, {}, workspace);
	return result.dispatch == telemetry::DispatchStatus::NotFound ? 0 : 1;
}
#elif defined(MODULE_RESOURCE)
#include <resource/Resource.hpp>

int main()
{
	return resource::has(resource::FileFlag::Readable, resource::FileFlag::Readable) ? 0 : 1;
}
#elif defined(MODULE_RESOURCE_PROTOCOL)
#include <resource/protocol/Protocol.hpp>

int main()
{
	std::array<std::byte, 8> out{};
	return resource::protocol::process({}, {}, out).written == 1 ? 0 : 1;
}
#elif defined(MODULE_CLIENT)
#include <client/DeviceClient.hpp>
#include <client/ThreadSafeClient.hpp>

int main()
{
	telemetry::client::RequestQueue<2, 64> queue;
	(void)queue.admit(1);
	queue.close();
	queue.markDrained();
	return queue.drained() ? 0 : 1;
}
#elif defined(MODULE_RESOURCE_ADAPTER)
#include <telemetry/descriptor/Descriptor.hpp>
#include <integration/telemetry/v3/DescriptorFile.hpp>
#include <integration/telemetry/v3/ValuesFile.hpp>

int main()
{
	telemetry::descriptor::Descriptor descriptor{model};
	resource::telemetry::v3::DescriptorFile file{descriptor};
	std::array<std::byte, 64> out{};
	const auto result = file.read(0, out);
	return result.status == resource::Status::Ok && result.written == 64 ? 0 : 1;
}
#elif defined(MODULE_ENCODED_SERVER)
#include <integration/encoded/Dispatcher.hpp>

int main()
{
	std::array<std::byte, 256> scratch{};
	telemetry::Workspace workspace{scratch};
	const auto result =
	    telemetry::integration::encoded::dispatchEncoded(model.view(), workspace, 1, {}, {});
	return result.status == telemetry::integration::encoded::EncodedDispatchStatus::Malformed ? 0
	                                                                                          : 1;
}
#elif defined(MODULE_RPC_SERVER) || defined(MODULE_RPC_SESSIONS)
#include <integration/telemetry/v3/RpcMailbox.hpp>
#if defined(MODULE_RPC_SESSIONS)
#include <integration/telemetry/v3/RpcSessionPool.hpp>
#endif
int main()
{
	auto view = model.view();
	std::array<std::byte, 256> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 64> request{}, response{};
	resource::telemetry::v3::RpcMailbox mailbox{view, workspace, request, response};
	(void)mailbox.process(0);
	return mailbox.state() == telemetry::protocol::rpc::RpcState::Idle ? 0 : 1;
}
#elif defined(MODULE_ENCODED_BACKEND)
#include <integration/encoded/Backend.hpp>
#include <integration/encoded/RetryingBackend.hpp>

int main()
{
	std::array<std::byte, 64> tx{}, rx{};
	telemetry::client::Clock clock{nullptr, +[](void*) noexcept -> std::uint32_t {
		                               return 0;
	                               }};
	telemetry::integration::encoded::EncodedBackend backend{tx, rx, clock};
	std::array<std::byte, 64> retryTx{}, retryRx{};
	telemetry::integration::encoded::RetryingEncodedBackend retry{
	    retryTx, retryRx, clock, {.replyTimeoutMs = 100, .maxRetries = 3}};
	return backend.openSession(1) && retry.openSession(1) ? 0 : 1;
}
#elif defined(MODULE_RPC_CLIENT) || defined(MODULE_RPC_BACKEND)
#include <client/rpc/RpcClient.hpp>
#if defined(MODULE_RPC_BACKEND)
#include <integration/rpc/Backend.hpp>
#endif
int main()
{
	std::array<std::byte, 64> request{}, result{}, packet{};
	telemetry::client::rpc::RpcClient client{0, request, result, packet};
#if defined(MODULE_RPC_BACKEND)
	telemetry::client::Clock clock{nullptr, +[](void*) noexcept -> std::uint32_t {
		                               return 0;
	                               }};
	telemetry::integration::rpc::RpcBackend backend{client, clock};
	return backend.openSession(1) ? 0 : 1;
#else
	return client.startSession(1) == telemetry::client::rpc::RpcClientStatus::Ok ? 0 : 1;
#endif
}
#endif
