/* Deterministic driver-borrow lifetime with actual Client/RPC/session dispatch. MIT. */
#include "TxStorage.hpp"
#include "Policy.hpp"
#include <integration/rpc/Backend.hpp>
#include <integration/telemetry/v3/RpcSessionPool.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include "../structured/rpc/Fixture.hpp"
#include <cstdio>

namespace tc = telemetry::client;
namespace server = resource::telemetry::v3;
namespace fixture = transport_dma_fixture;
static unsigned checks = 0, failures = 0;

static void check(bool value, unsigned line) noexcept
{
	++checks;
	if (!value) {
		++failures;
		std::fprintf(stderr, "transport_dma line=%u failed\n", line);
	}
}

#define CHECK(value) check(bool(value), __LINE__)

struct Harness {
	std::uint32_t now = 0, generation = 1;
	std::array<std::byte, 512> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 256> serverRequest{}, serverResult{};
	server::RpcMailbox mailbox{rpc_fixture::view, workspace, serverRequest, serverResult};
	const resource::FileSystem<1> files{resource::filesystem(resource::file("/rpc", mailbox))};
	server::RpcSessionPool<1> pool{std::array{server::RpcSessionBinding{mailbox, files.view()}}};
	server::RpcSessionHandle session = pool.acquire({7, generation, 0}).session;
	std::array<std::byte, 256> request{}, result{};
	std::array<std::byte, 40> packet{};
	tc::rpc::RpcClient native{0, request, result, packet, {3, 80, 1, 2}};
	tc::Clock clock{this, +[](void* p) noexcept {
		                return static_cast<Harness*>(p)->now;
	                }};
	telemetry::integration::rpc::RpcBackend backend{native, clock};
	fixture::Client client{backend, clock};
	fixture::TxStorage tx;
	std::array<std::byte, 96> reply{}, retainedReply{};
	std::size_t retainedBytes = 0;
	tc::Packet retainedTicket{};
	unsigned callbacks = 0;
	tc::Status completion = tc::Status::Busy;

	Harness()
	{
		rpc_fixture::device = {};
		const telemetry::descriptor::Descriptor descriptor{rpc_fixture::model};
		const auto read = descriptor.read(0, fixture::Policy::observedHeader);
		CHECK(read.written == 64);
		CHECK(client.admitDescriptor(fixture::Policy::observedHeader, generation) ==
		      tc::Status::Ok);
	}

	~Harness()
	{
		client.close();
		for (unsigned i = 0; i < 12 && !client.drained(); ++i)
			client.process(++now);
		CHECK(client.drained());
		// Test driver completion is explicit even when logical work has drained.
		if (tx.state() == fixture::TxStorage::State::DriverBorrow)
			CHECK(tx.requestAbort());
		if (tx.state() == fixture::TxStorage::State::AbortPending)
			CHECK(tx.abortComplete(tx.lease()));
		if (tx.state() == fixture::TxStorage::State::Copied)
			CHECK(tx.discardUnsent());
		CHECK(pool.release(session) == server::RpcSessionStatus::Ok);
		(void)pool.process(now);
	}

	tc::Submission command(std::uint32_t timeout = 40)
	{
		callbacks = 0;
		completion = tc::Status::Busy;
		return client.submitEncoded(
		    {tc::Operation::Command, 1, {}, 0},
		    [this](const tc::ResultView& r) noexcept {
			    ++callbacks;
			    completion = r.status;
		    },
		    timeout);
	}

	bool copy()
	{
		return tx.copy(client.nextPacket());
	}

	bool start()
	{
		const auto offered = tx.packet();
		if (!client.prepareHandoff(offered.session, offered.requestId)) {
			CHECK(tx.discardUnsent());
			return false;
		}
		CHECK(tx.driverBorrow());
		CHECK(client.onSendAccepted(offered.session, offered.requestId));
		return true;
	}

	void serverReceive(bool acknowledge = true, bool deliver = true)
	{
		const auto offered = tx.packet();
		const auto response = pool.handle(session, offered.bytes, reply);
		CHECK(response.status == server::RpcSessionStatus::Ok &&
		      response.reply.status == resource::Status::Ok);
		if (response.ticket.serial && acknowledge)
			CHECK(pool.replyAccepted(session, response.ticket) == server::RpcSessionStatus::Ok);
		retainedTicket = offered;
		retainedBytes = response.reply.written;
		std::copy_n(reply.begin(), retainedBytes, retainedReply.begin());
		if (deliver)
			CHECK(client.onReply(tc::Input{reply}.first(retainedBytes), offered.session,
			                     offered.requestId));
		(void)pool.process(now);
	}

	void step()
	{
		if (tx.state() == fixture::TxStorage::State::Idle && copy()) {
			if (start()) {
				serverReceive();
				CHECK(tx.driverComplete(tx.lease()));
			}
		}
		(void)pool.process(now);
		client.process(++now);
	}

	void finish()
	{
		for (unsigned i = 0; i < 100 && !callbacks; ++i)
			step();
		CHECK(callbacks == 1);
	}

	void recover()
	{
		CHECK(client.recoveryRequired());
		CHECK(tx.state() == fixture::TxStorage::State::Idle);
		CHECK(client.pending() == 0);
		CHECK(pool.release(session) == server::RpcSessionStatus::Ok);
		(void)pool.process(now);
		const auto old = generation++;
		session = pool.acquire({7, generation, 0}).session;
		CHECK(pool.valid(session));
		CHECK(!client.confirmSessionClosed(old + 99));
		CHECK(client.confirmSessionClosed(old));
		CHECK(client.admitDescriptor(fixture::Policy::observedHeader, generation) ==
		      tc::Status::Ok);
	}
};

static void unsentCancellation()
{
	Harness h;
	const auto submitted = h.command();
	CHECK(submitted);
	CHECK(h.copy());
	CHECK(h.client.cancel(submitted.handle));
	CHECK(!h.start());
	h.client.process(h.now);
	CHECK(h.callbacks == 1 && h.completion == tc::Status::Cancelled);
	CHECK(!h.client.recoveryRequired() && rpc_fixture::device.commands == 0);
}

static void borrowedCancellation(tc::Status reason, bool afterDelivery)
{
	Harness h;
	const auto submitted = h.command(12);
	CHECK(submitted && h.copy() && h.start());
	std::array<std::byte, fixture::TxStorage::capacity> saved{};
	const auto bytes = h.tx.packet().bytes;
	std::copy(bytes.begin(), bytes.end(), saved.begin());
	if (afterDelivery)
		h.serverReceive(true, false);
	if (reason == tc::Status::Timeout)
		h.now = 12;
	else if (reason == tc::Status::SessionLost)
		h.backend.close();
	else if (reason == tc::Status::TransportError)
		h.backend.cancel(reason);
	else
		CHECK(h.client.cancel(submitted.handle));
	h.client.process(h.now);
	CHECK(h.callbacks == 1 && h.completion == tc::Status::OutcomeUnknown);
	CHECK(h.client.recoveryRequired());
	CHECK(!h.tx.copy({tc::Input{saved}.first(8), 9, 9}));
	CHECK(std::equal(bytes.begin(), bytes.end(), saved.begin()));
	CHECK(h.tx.requestAbort());
	CHECK(!h.tx.copy({tc::Input{saved}.first(8), 9, 9}));
	CHECK(std::equal(bytes.begin(), bytes.end(), saved.begin()));
	const auto oldLease = h.tx.lease();
	CHECK(h.tx.abortComplete(oldLease));
	const auto oldTicket = h.retainedTicket;
	const auto oldSize = h.retainedBytes;
	const auto effects = rpc_fixture::device.commands;
	h.recover();
	CHECK(h.command());
	CHECK(!h.client.onReply(tc::Input{h.retainedReply}.first(oldSize), oldTicket.session,
	                        oldTicket.requestId));
	CHECK(h.copy() && h.start());
	CHECK(!h.tx.driverComplete(oldLease));
	CHECK(!h.tx.abortComplete(oldLease));
	h.serverReceive();
	CHECK(h.tx.driverComplete(h.tx.lease()));
	h.finish();
	CHECK(h.completion == tc::Status::Ok && rpc_fixture::device.commands == effects + 1);
}

static void lostFinalAcknowledgement()
{
	Harness h;
	CHECK(h.command() && h.copy() && h.start());
	const auto first = h.tx.packet();
	std::array<std::byte, 40> saved{};
	std::copy(first.bytes.begin(), first.bytes.end(), saved.begin());
	h.serverReceive(true, false); // Server TX accepted ACK; client never receives it.
	CHECK(rpc_fixture::device.commands == 1);
	CHECK(h.tx.driverComplete(h.tx.lease()));
	h.now = 3;
	h.client.process(h.now);
	CHECK(h.copy());
	const auto repeated = h.tx.packet();
	CHECK(repeated.session == first.session && repeated.requestId == first.requestId);
	CHECK(repeated.bytes.size() == first.bytes.size());
	CHECK(std::equal(repeated.bytes.begin(), repeated.bytes.end(), saved.begin()));
	CHECK(h.start());
	h.serverReceive();
	CHECK(h.tx.driverComplete(h.tx.lease()));
	h.finish();
	CHECK(h.completion == tc::Status::Ok && rpc_fixture::device.commands == 1);
}

static void preparedButNotAccepted()
{
	Harness h;
	const auto submitted = h.command();
	CHECK(submitted && h.copy());
	const auto packet = h.tx.packet();
	CHECK(h.client.prepareHandoff(packet.session, packet.requestId));
	CHECK(h.client.cancel(submitted.handle));
	h.client.process(h.now);
	CHECK(h.callbacks == 1 && h.completion == tc::Status::OutcomeUnknown);
	CHECK(rpc_fixture::device.commands == 0);
	CHECK(h.tx.state() == fixture::TxStorage::State::Copied);
	CHECK(h.tx.discardUnsent());
	CHECK(h.client.recoveryRequired());
	h.recover();
}

int main()
{
	unsentCancellation();
	preparedButNotAccepted();
	for (const auto reason : {tc::Status::Cancelled, tc::Status::Timeout, tc::Status::SessionLost,
	                          tc::Status::TransportError})
		for (const bool delivered : {false, true})
			borrowedCancellation(reason, delivered);
	lostFinalAcknowledgement();
	std::printf("transport_dma checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
