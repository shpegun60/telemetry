/*
 * @file Integration.cpp
 * @brief Independent wire-error/value-status and stale-storage integration checks.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "../dynamic_client/Fixture.hpp"
#include <memory>
#include <vector>

using namespace dynamic_fixture;

namespace {
void malformedWireKeepsCurrentBinding() noexcept
{
	Encoded h;
	Observed result;
	const auto endpoint = h.bind(cl::Operation::FieldRead, 0);
	CHECK(h.client.readField(endpoint, [&](const dy::DynamicResult& value) noexcept {
		result(value);
	}));
	const auto packet = h.offered();
	CHECK(h.client.prepareHandoff(packet.session, packet.requestId));
	CHECK(h.client.onSendAccepted(packet.session, packet.requestId));
	const std::array<std::byte, 3> malformed{std::byte{0x7b}, std::byte{0}, std::byte{0x7b}};
	CHECK(h.client.onReply(malformed, packet.session, packet.requestId));
	CHECK(result.calls == 1);
	CHECK(result.status == cl::Status::MalformedReply);
	CHECK(result.valueStatus == dy::ValueStatus::NoValue);
	CHECK(!result.hasValue);
	CHECK(h.metadata.schema.valid());
	CHECK(h.client.recoveryRequired());
	CHECK(h.client.isCurrent(endpoint));
	CHECK(h.client
	          .submit(endpoint, {},
	                  [](const dy::DynamicResult&) noexcept {
	                  })
	          .status == cl::Status::RecoveryRequired);
	h.client.close();
	h.client.process(h.now);
	CHECK(h.client.drained());
}

void canonicalFailureKeepsOperationOutcome() noexcept
{
	Encoded h;
	Observed result;
	const auto endpoint = h.bind(cl::Operation::FieldRead, 0);
	CHECK(h.client.readField(endpoint, [&](const dy::DynamicResult& value) noexcept {
		result(value);
	}));
	const auto packet = h.offered();
	CHECK(h.client.prepareHandoff(packet.session, packet.requestId));
	CHECK(h.client.onSendAccepted(packet.session, packet.requestId));
	const auto reply =
	    en::dispatchEncoded(model.view(), h.workspace, packet.session, packet.bytes, h.reply);
	CHECK(reply.status == en::EncodedDispatchStatus::Ok);
	CHECK(reply.written >= 5);
	h.reply[reply.written - 1] = std::byte{2};
	CHECK(h.client.onReply(cl::Input{h.reply}.first(reply.written), packet.session,
	                       packet.requestId));
	CHECK(result.calls == 1 && result.status == cl::Status::Ok);
	CHECK(result.valueStatus == dy::ValueStatus::InvalidPayload && !result.hasValue);
	CHECK(h.client.isCurrent(endpoint) && !h.client.recoveryRequired());
	h.client.close();
	h.client.process(h.now);
	CHECK(h.client.drained());
}

struct HeapMetadata {
	std::unique_ptr<rt::RuntimeType[]> types{new rt::RuntimeType[64]{}};
	std::unique_ptr<rt::RuntimeMember[]> members{new rt::RuntimeMember[64]{}};
	std::unique_ptr<rt::RuntimeEnumEntry[]> enums{new rt::RuntimeEnumEntry[8]{}};
	std::unique_ptr<rt::RuntimeCatalog[]> catalogs{new rt::RuntimeCatalog[8]{}};
	std::unique_ptr<rt::RuntimeEndpoint[]> endpoints{new rt::RuntimeEndpoint[32]{}};
	rt::RuntimeSchemaWorkspace workspace{{types.get(), 64},
	                                     {members.get(), 64},
	                                     {enums.get(), 8},
	                                     {catalogs.get(), 8},
	                                     {endpoints.get(), 32}};
	std::unique_ptr<std::vector<std::byte>> descriptor{
	    std::make_unique<std::vector<std::byte>>(bytes.begin(), bytes.end())};
	rt::RuntimeSchema schema{};

	HeapMetadata()
	{
		const auto parsed = rt::RuntimeSchema::parse(*descriptor, workspace);
		CHECK(parsed);
		schema = parsed.schema;
	}

	void invalidateAndRelease()
	{
		CHECK(workspace.reset() == rt::Status::Ok);
		descriptor.reset();
		types.reset();
		members.reset();
		enums.reset();
		catalogs.reset();
		endpoints.reset();
	}
};

void releasedMetadataIsNotInspected() noexcept
{
	for (bool sent : {false, true}) {
		HeapMetadata metadata;
		std::uint32_t now = 0;
		cl::Clock clock{&now, +[](void* p) noexcept {
			                return *static_cast<std::uint32_t*>(p);
		                }};
		std::array<std::byte, 1024> tx{}, rx{}, port{}, reply{}, scratch{};
		telemetry::Workspace workspace{scratch};
		en::EncodedBackend backend{tx, rx, clock};
		dy::DynamicClient<4, 64> client{backend, clock};
		CHECK(client.admit(metadata.schema, cl::Input{bytes}.first(64), 1) == cl::Status::Ok);
		const auto binding = client.bind(cl::Operation::FieldRead, 0);
		CHECK(binding);
		Observed observed;
		CHECK(client.readField(binding.endpoint, [&](const dy::DynamicResult& r) noexcept {
			observed(r);
		}));
		std::size_t replyBytes = 0;
		cl::Packet packet;
		if (sent) {
			packet = client.nextPacket();
			CHECK(packet);
			CHECK(packet.bytes.size() <= port.size());
			std::copy(packet.bytes.begin(), packet.bytes.end(), port.begin());
			packet.bytes = cl::Input{port}.first(packet.bytes.size());
			CHECK(client.prepareHandoff(packet.session, packet.requestId));
			CHECK(client.onSendAccepted(packet.session, packet.requestId));
			const auto dispatched =
			    en::dispatchEncoded(model.view(), workspace, packet.session, packet.bytes, reply);
			CHECK(dispatched.status == en::EncodedDispatchStatus::Ok);
			replyBytes = dispatched.written;
		}
		metadata.invalidateAndRelease();
		CHECK(!metadata.schema.valid());
		CHECK(!client.isCurrent(binding.endpoint));
		if (sent)
			CHECK(client.onReply(cl::Input{reply}.first(replyBytes), packet.session,
			                     packet.requestId));
		else
			CHECK(!client.nextPacket());
		client.process(now);
		CHECK(observed.calls == 1 && !observed.hasValue);
		CHECK(observed.valueStatus == dy::ValueStatus::Stale);
		CHECK(observed.status == (sent ? cl::Status::Ok : cl::Status::Cancelled));
		CHECK(client
		          .readField(binding.endpoint,
		                     [](const auto&) noexcept {
		                     })
		          .status == cl::Status::NotAdmitted);
		client.close();
		client.process(now);
		CHECK(client.drained());
	}
}
} // namespace

int main()
{
	malformedWireKeepsCurrentBinding();
	canonicalFailureKeepsOperationOutcome();
	releasedMetadataIsNotInspected();
	std::printf("independent dynamic integration: checks=%u failures=0\n", checks);
}