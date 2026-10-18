/* Counted raw/native and portable-backend RPC integration. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#include <telemetry/client/rpc/RpcBackend.hpp>
#include <resource/telemetry/v3/RpcSessionPool.hpp>
#include "../structured/rpc/Fixture.hpp"
#include <cstdio>

namespace tc = telemetry::client;
namespace rpc = resource::telemetry::v3;
using rpc_fixture::Config;
using rpc_fixture::device;
using tc::Operation;
using tc::Status;
static unsigned checks = 0, failures = 0;

static void check(bool value, unsigned line) noexcept
{
	++checks;
	if (!value) {
		++failures;
		std::fprintf(stderr, "async rpc line=%u check=%u failed\n", line, checks);
	}
}

#define CHECK(value) check(bool(value), __LINE__)

struct Harness {
	std::array<std::byte, 512> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 256> serverRequest{}, serverResult{};
	rpc::RpcMailbox mailbox{rpc_fixture::view, workspace, serverRequest, serverResult};
	const resource::FileSystem<1> files{resource::filesystem(resource::file("/rpc", mailbox))};
	rpc::RpcSessionPool<1> pool{std::array{rpc::RpcSessionBinding{mailbox, files.view()}}};
	rpc::RpcSessionHandle handle = pool.acquire({10, 1, 0}).session;
	std::array<std::byte, 256> request{}, result{};
	std::array<std::byte, 40> packet{};
	rpc::RpcClient native{0, request, result, packet, {3, 40, 1, 1}};
	std::uint32_t now = 0;
	tc::rpc::RpcBackend backend{native, {this, [](void* self) noexcept {
		                                     return static_cast<Harness*>(self)->now;
	                                     }}};
	std::array<std::byte, 48> response{};
	std::array<std::byte, 40> portCopy{};

	Harness() noexcept
	{
		CHECK(backend.openSession(1));
	}

	bool exchange(bool acknowledge = true, bool deliver = true) noexcept
	{
		backend.process(now);
		const auto offered = backend.nextPacket();
		if (!offered)
			return false;
		std::copy(offered.bytes.begin(), offered.bytes.end(), portCopy.begin());
		const auto bytes = tc::Input{portCopy}.first(offered.bytes.size());
		CHECK(backend.prepareHandoff(offered.session, offered.requestId));
		CHECK(backend.onSendAccepted(offered.session, offered.requestId));
		const auto reply = pool.handle(handle, bytes, response);
		CHECK(reply.status == rpc::RpcSessionStatus::Ok);
		CHECK(reply.reply.status == resource::Status::Ok);
		if (reply.ticket.serial && acknowledge)
			CHECK(pool.replyAccepted(handle, reply.ticket) == rpc::RpcSessionStatus::Ok);
		if (deliver)
			CHECK(backend.onReply(tc::Input{response}.first(reply.reply.written), offered.session,
			                      offered.requestId));
		(void)pool.process(now);
		++now;
		return true;
	}

	void run() noexcept
	{
		for (unsigned i = 0; i != 100 && !backend.completed(); ++i) {
			if (!exchange()) {
				(void)pool.process(now);
				backend.process(++now);
			}
		}
		CHECK(backend.completed());
	}

	void recover(std::uint32_t next) noexcept
	{
		CHECK(backend.recoveryRequired());
		CHECK(!backend.openSession(next));
		CHECK(!backend.confirmSessionClosed(backend.session() + 1));
		CHECK(pool.release(handle) == rpc::RpcSessionStatus::Ok);
		(void)pool.process(now);
		handle = pool.acquire({10, next, 0}).session;
		CHECK(pool.valid(handle));
		CHECK(backend.confirmSessionClosed(backend.session()));
		backend.releaseResult();
		CHECK(backend.openSession(next));
	}
};

static auto encoded(Config config) noexcept
{
	std::array<std::byte, telemetry::wireSize<Config>> bytes{};
	CHECK(telemetry::encode(config, bytes) == telemetry::CodecStatus::Ok);
	return bytes;
}

static void operations() noexcept
{
	device = {};
	Harness h;
	auto input = encoded({91, false});
	CHECK(h.backend.start({Operation::FieldWrite, 0, input, 0}, h.now) == Status::Ok);
	input = encoded({999, true}); // The native engine has its own exact payload snapshot.
	h.run();
	CHECK(h.backend.result().succeeded() && device.current.value == 91 && device.writes == 1);
	CHECK(h.backend.start({Operation::FieldRead, 0, {}, 5}, h.now) == Status::Busy);
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::FieldRead, 0, {}, 5}, h.now) == Status::Ok);
	h.run();
	Config value{};
	CHECK(telemetry::decode(h.backend.result().payload, value) == telemetry::CodecStatus::Ok);
	CHECK(value.value == 91 && !value.enabled && device.reads == 1);
	CHECK(h.native.decodeResult(value).status == rpc::RpcClientStatus::WrongType);
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::Command, 1, {}, 0}, h.now) == Status::Ok);
	h.run();
	CHECK(h.backend.result().executed() && device.commands == 1);
	h.backend.releaseResult();
	device.commandStatus = telemetry::CommandResult::Accepted;
	CHECK(h.backend.start({Operation::Command, 1, {}, 0}, h.now) == Status::Ok);
	h.run();
	CHECK(h.backend.result().accepted() && !h.backend.result().executed());
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::Service, 0, input, 5}, h.now) == Status::Ok);
	h.run();
	CHECK(h.backend.result().succeeded() && device.services == 1);
	CHECK(telemetry::decode(h.backend.result().payload, value) == telemetry::CodecStatus::Ok);
	CHECK(value.value == 999 && value.enabled);
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::Service, 1, {}, 0}, h.now) == Status::Ok);
	h.run();
	CHECK(h.backend.result().succeeded() && h.backend.result().payload.empty());
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::FieldWrite, 1, input, 0}, h.now) == Status::Ok);
	h.run();
	CHECK(!h.backend.result().succeeded());
	CHECK(h.backend.result().endpointStatus == unsigned(telemetry::WriteResult::ReadOnly));
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::FieldRead, 100, {}, 5}, h.now) == Status::Ok);
	h.run();
	CHECK(!h.backend.result().succeeded() && h.backend.result().dispatch != 0);
	CHECK(!h.backend.recoveryRequired());
}

static void rawPreflight() noexcept
{
	Harness h;
	CHECK(h.native.beginEncoded(static_cast<rpc::RpcOperation>(0), 0, {}, 0) ==
	      rpc::RpcClientStatus::InvalidRequest);
	const auto bytes = encoded({1, true});
	CHECK(h.native.beginEncoded(rpc::RpcOperation::FieldRead, 0, bytes, 5) ==
	      rpc::RpcClientStatus::InvalidRequest);
	CHECK(h.native.beginEncoded(rpc::RpcOperation::Command, 0, bytes, 1) ==
	      rpc::RpcClientStatus::InvalidRequest);
	CHECK(h.native.beginEncoded(rpc::RpcOperation::Service, 0, h.request, 5) ==
	      rpc::RpcClientStatus::BufferTooSmall);
	CHECK(h.native.beginEncoded(rpc::RpcOperation::Service, 0, tc::Input{h.result}.first(5), 5) ==
	      rpc::RpcClientStatus::Overlap);
	CHECK(h.native.beginEncoded(rpc::RpcOperation::Service, 0, bytes, 300) ==
	      rpc::RpcClientStatus::BufferTooSmall);
	CHECK(h.native.state() == rpc::RpcClientState::Idle);
}

static void freshDeadlineGate() noexcept
{
	Harness h;
	CHECK(h.backend.start({Operation::Command, 1, {}, 0, 2}, h.now) == Status::Ok);
	const auto packet = h.backend.nextPacket();
	CHECK(packet && packet.mayExecute);
	h.now = 3; // No process() tick occurred after the port copied the packet.
	CHECK(!h.backend.prepareHandoff(packet.session, packet.requestId));
	CHECK(h.backend.completed() && h.backend.result().status == Status::Timeout);
	CHECK(!h.backend.recoveryRequired());
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::Command, 1, {}, 0, 2}, h.now) == Status::Ok);
	const auto next = h.backend.nextPacket();
	CHECK(h.backend.prepareHandoff(next.session, next.requestId));
	h.now = 6;
	CHECK(h.backend.onSendAccepted(next.session, next.requestId));
	CHECK(h.backend.result().status == Status::OutcomeUnknown && h.backend.recoveryRequired());
}

static void cancellationAndRecovery() noexcept
{
	device = {};
	Harness h;
	const auto input = encoded({12, true});
	CHECK(h.backend.start({Operation::Command, 0, input, 0}, h.now) == Status::Ok);
	h.backend.cancel(Status::Timeout);
	CHECK(h.backend.completed() && h.backend.result().status == Status::Timeout);
	CHECK(!h.backend.recoveryRequired() && device.commands == 0);
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::Command, 0, input, 0}, h.now) == Status::Ok);
	CHECK(h.exchange()); // First non-final request fragment.
	const auto final = h.backend.nextPacket();
	CHECK(final && final.bytes[13] == std::byte{1});
	CHECK(h.backend.prepareHandoff(final.session, final.requestId));
	h.backend.cancel(Status::Timeout); // The port may have sent; native acceptance is still absent.
	CHECK(h.backend.result().status == Status::OutcomeUnknown);
	CHECK(h.backend.recoveryRequired() && device.commands == 0);
	CHECK(!h.backend.nextPacket());
	CHECK(!h.backend.onSendAccepted(final.session, final.requestId));
	h.recover(2);
	CHECK(h.backend.start({Operation::Command, 1, {}, 0}, h.now) == Status::Ok);
	h.run();
	CHECK(device.commands == 1 && h.backend.result().executed());
	h.backend.releaseResult();
	h.backend.close();
	CHECK(h.backend.recoveryRequired());
	h.recover(3);
	CHECK(h.backend.ready() && device.commands == 1); // No replay during recovery.
}

static void withheldAcknowledgement(bool read) noexcept
{
	device = {};
	Harness h;
	const auto input = encoded({12, true});
	CHECK(h.backend.start({read ? Operation::FieldRead : Operation::Command, 0,
	                       read ? tc::Input{} : tc::Input{input}, read ? 5u : 0u},
	                      h.now) == Status::Ok);
	if (!read)
		CHECK(h.exchange());
	CHECK(h.exchange(false)); // Final WRITE ACK reaches client, but server handoff is unconfirmed.
	CHECK(h.mailbox.state() == rpc::RpcState::Pending);
	for (unsigned i = 0; i != 60 && !h.backend.completed(); ++i) {
		if (!h.exchange(false)) {
			(void)h.pool.process(h.now);
			h.backend.process(++h.now);
		}
	}
	CHECK(h.backend.completed() && h.backend.recoveryRequired());
	CHECK(h.backend.result().status == (read ? Status::Timeout : Status::OutcomeUnknown));
	CHECK(h.mailbox.state() == rpc::RpcState::Pending && rpc_fixture::callbacks() == 0);
	const auto existing = h.pool.acquire({10, 1, 0});
	CHECK(existing.status == rpc::RpcSessionStatus::Existing && existing.session == h.handle);
	h.recover(2);
	CHECK(h.backend.start({Operation::Command, 1, {}, 0}, h.now) == Status::Ok);
	h.run();
	CHECK(h.backend.result().executed() && device.commands == 1);
}

static void retryAndMissingGate() noexcept
{
	device = {};
	Harness h;
	const auto input = encoded({12, true});
	CHECK(h.backend.start({Operation::Command, 0, input, 0}, h.now) == Status::Ok);
	CHECK(h.exchange());
	const auto first = h.backend.nextPacket();
	std::array<std::byte, 40> saved{};
	std::copy(first.bytes.begin(), first.bytes.end(), saved.begin());
	CHECK(h.exchange(true, false)); // Lost ACK after actual server dispatch.
	CHECK(device.commands == 1);
	h.now += 3;
	h.backend.process(h.now);
	const auto retry = h.backend.nextPacket();
	CHECK(retry && retry.requestId == first.requestId && retry.session == first.session);
	CHECK(std::equal(retry.bytes.begin(), retry.bytes.end(), saved.begin()));
	CHECK(h.exchange());
	h.run();
	CHECK(device.commands == 1 && h.backend.result().executed());
	h.backend.releaseResult();
	CHECK(h.backend.start({Operation::Command, 0, input, 0}, h.now) == Status::Ok);
	CHECK(h.exchange());
	const auto final = h.backend.nextPacket();
	CHECK(final && !h.backend.onSendAccepted(final.session, final.requestId));
	CHECK(h.backend.recoveryRequired() && h.backend.result().status == Status::OutcomeUnknown);
}

int main()
{
	operations();
	rawPreflight();
	freshDeadlineGate();
	cancellationAndRecovery();
	withheldAcknowledgement(false);
	withheldAcknowledgement(true);
	retryAndMissingGate();
	std::printf("async_rpc checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
