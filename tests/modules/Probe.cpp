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
#elif defined(MODULE_RUNTIME)
#include <telemetry/runtime/DynamicValueView.hpp>

int main()
{
	namespace rt = telemetry::runtime;
	// Construct the frozen empty v3 registry without importing its producer.
	std::array<std::byte, 348> descriptor{};
	auto number = [&](std::size_t offset, std::uint64_t value, unsigned width) {
		for (unsigned i = 0; i < width; ++i)
			descriptor[offset + i] = std::byte((value >> (8 * i)) & 0xffu);
	};
	descriptor[0] = std::byte{'T'};
	descriptor[1] = std::byte{'D'};
	descriptor[2] = std::byte{'S'};
	descriptor[3] = std::byte{'3'};
	number(4, 3, 2);
	number(8, 64, 2);
	number(12, descriptor.size(), 4);
	number(24, 12, 4);
	number(52, 64, 4);
	number(56, descriptor.size(), 4);
	number(60, descriptor.size(), 4);
	constexpr unsigned sizes[]{0, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
	std::size_t offset = 64;
	for (unsigned id = 0; id < 12; ++id) {
		number(offset, 1, 1);
		number(offset + 1, 1, 1);
		number(offset + 4, id ? 16 : 12, 4);
		number(offset + 8, id, 4);
		number(offset + 12, id ? 1 : 0, 1);
		number(offset + 16, sizes[id], 4);
		if (id)
			number(offset + 20, id, 1);
		offset += id ? 24 : 20;
	}
	auto hash = telemetry::descriptor::fingerprintBasis;
	for (auto value : descriptor)
		hash =
		    (hash ^ std::to_integer<std::uint8_t>(value)) * telemetry::descriptor::fingerprintPrime;
	number(16, hash, 8);
	std::array<rt::RuntimeType, 12> types{};
	std::array<rt::RuntimeMember, 0> members{};
	std::array<rt::RuntimeEnumEntry, 0> enums{};
	std::array<rt::RuntimeCatalog, 0> catalogs{};
	std::array<rt::RuntimeEndpoint, 0> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	const auto parsed = rt::RuntimeSchema::parse(descriptor, workspace);
	if (parsed.status != rt::Status::Ok)
		return 1;
	const std::array payload{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}};
	const auto value = parsed.schema.view(6, payload);
	if (!value)
		return 2;
	const auto scalar = value.value.get<std::uint32_t>();
	return scalar && scalar.value == 0x78563412u ? 0 : 3;
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
