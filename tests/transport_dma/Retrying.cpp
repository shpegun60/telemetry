/* Retrying TEQ1 with real dispatch and a driver-owned TX copy. MIT. */
#include "Policy.hpp"
#include "TxStorage.hpp"
#include "../structured/rpc/Fixture.hpp"
#include <integration/encoded/Dispatcher.hpp>
#include <integration/encoded/RetryingBackend.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <cstdio>

namespace tc = telemetry::client;
namespace enc = telemetry::integration::encoded;
namespace fixture = transport_dma_fixture;
static unsigned checks = 0, failures = 0;

static void check(bool value, unsigned line) noexcept
{
	++checks;
	if (!value) {
		++failures;
		std::fprintf(stderr, "transport_dma_retry line=%u failed\n", line);
	}
}

#define CHECK(value) check(bool(value), __LINE__)

struct Harness {
	std::uint32_t now = 0;
	std::array<std::byte, 512> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 256> tx{}, result{}, reply{};
	tc::Clock clock{this, +[](void* self) noexcept {
		                return static_cast<Harness*>(self)->now;
	                }};
	enc::RetryingEncodedBackend backend{tx, result, clock, {.replyTimeoutMs = 3, .maxRetries = 2}};
	fixture::Client client{backend, clock};
	fixture::TxStorage storage;
	unsigned callbacks = 0;
	tc::Status status = tc::Status::Busy;
	std::size_t replyBytes = 0;

	Harness()
	{
		rpc_fixture::device = {};
		const telemetry::descriptor::Descriptor descriptor{rpc_fixture::model};
		CHECK(descriptor.read(0, fixture::Policy::observedHeader).written == 64);
		CHECK(client.admitDescriptor(fixture::Policy::observedHeader, 1) == tc::Status::Ok);
	}

	~Harness()
	{
		client.close();
		for (unsigned i = 0; i < 12 && !client.drained(); ++i)
			client.process(++now);
		CHECK(client.drained());
		if (storage.state() == fixture::TxStorage::State::DriverBorrow)
			CHECK(storage.requestAbort());
		if (storage.state() == fixture::TxStorage::State::AbortPending)
			CHECK(storage.abortComplete(storage.lease()));
		if (storage.state() == fixture::TxStorage::State::Copied)
			CHECK(storage.discardUnsent());
	}

	tc::Submission command(std::uint32_t timeout = 30)
	{
		return client.submitEncoded(
		    {tc::Operation::Command, 1, {}, 0},
		    [this](const tc::ResultView& value) noexcept {
			    ++callbacks;
			    status = value.status;
		    },
		    timeout);
	}

	tc::Packet send()
	{
		const auto packet = client.nextPacket();
		CHECK(packet && storage.copy(packet));
		CHECK(client.prepareHandoff(packet.session, packet.requestId));
		CHECK(storage.driverBorrow());
		CHECK(client.onSendAccepted(packet.session, packet.requestId));
		return packet;
	}

	void dispatch()
	{
		const auto result =
		    enc::dispatchEncoded(rpc_fixture::view, workspace, 1, storage.packet().bytes, reply);
		CHECK(result.status == enc::EncodedDispatchStatus::Ok);
		replyBytes = result.written;
	}

	void receive(tc::Packet packet)
	{
		CHECK(client.onReply(tc::Input{reply}.first(replyBytes), packet.session, packet.requestId));
		client.process(now);
	}
};

static void repeatedExecutionIsExplicit()
{
	Harness h;
	CHECK(h.command());
	const auto first = h.send();
	std::array<std::byte, 128> copy{};
	std::copy(first.bytes.begin(), first.bytes.end(), copy.begin());
	h.dispatch(); // Result is lost, unlike RPC there is no server deduplication.
	CHECK(rpc_fixture::device.commands == 1);
	h.now = 3;
	h.client.process(h.now);
	const auto offered = h.client.nextPacket();
	CHECK(offered && offered.session == first.session && offered.requestId == first.requestId);
	CHECK(!h.storage.copy(offered)); // Retry available, physical driver still owns TX.
	CHECK(std::equal(copy.begin(), copy.begin() + first.bytes.size(),
	                 h.storage.packet().bytes.begin()));
	const auto oldLease = h.storage.lease();
	CHECK(h.storage.driverComplete(oldLease));
	const auto retry = h.send();
	CHECK(h.storage.lease() != oldLease);
	CHECK(!h.storage.driverComplete(oldLease)); // Same wire ID is a fresh physical TX lease.
	CHECK(retry.bytes.size() == first.bytes.size() &&
	      std::equal(retry.bytes.begin(), retry.bytes.end(), copy.begin()));
	h.dispatch();
	CHECK(rpc_fixture::device.commands == 2);
	h.receive(retry);
	CHECK(h.callbacks == 1 && h.status == tc::Status::Ok);
	CHECK(!h.client.nextPacket());
	CHECK(h.storage.driverComplete(h.storage.lease()));
}

static void lateReplyStopsRetry(bool prepared)
{
	Harness h;
	CHECK(h.command());
	const auto first = h.send();
	h.dispatch();
	CHECK(h.storage.driverComplete(h.storage.lease()));
	h.now = 3;
	h.client.process(h.now);
	const auto retry = h.client.nextPacket();
	CHECK(retry && h.storage.copy(retry));
	if (prepared)
		CHECK(h.client.prepareHandoff(retry.session, retry.requestId));
	h.receive(first); // The earlier checked reply remains useful between attempts.
	CHECK(h.callbacks == 1 && h.status == tc::Status::Ok);
	CHECK(!h.client.prepareHandoff(retry.session, retry.requestId));
	CHECK(h.storage.discardUnsent());
	CHECK(rpc_fixture::device.commands == 1 && !h.client.nextPacket());
}

static void retryFreshGate(bool expired)
{
	Harness h;
	const auto submitted = h.command(7);
	CHECK(submitted);
	const auto first = h.send();
	h.now = 3;
	h.client.process(h.now);
	const auto retry = h.client.nextPacket();
	CHECK(retry && retry.requestId == first.requestId);
	CHECK(h.storage.driverComplete(h.storage.lease()));
	CHECK(h.storage.copy(retry));
	if (expired)
		h.now = 7;
	else
		CHECK(h.client.cancel(submitted.handle));
	CHECK(!h.client.prepareHandoff(retry.session, retry.requestId));
	h.client.process(h.now);
	CHECK(h.callbacks == 1 && h.status == tc::Status::OutcomeUnknown);
	CHECK(h.client.recoveryRequired());
	CHECK(h.storage.discardUnsent());
	CHECK(rpc_fixture::device.commands == 0);
}

static void timeoutRetainsBorrow()
{
	Harness h;
	CHECK(h.command(5));
	const auto packet = h.send();
	std::array<std::byte, 128> copy{};
	std::copy(packet.bytes.begin(), packet.bytes.end(), copy.begin());
	h.now = 5;
	h.client.process(h.now);
	CHECK(h.callbacks == 1 && h.status == tc::Status::OutcomeUnknown);
	CHECK(h.client.recoveryRequired() && !h.client.nextPacket());
	CHECK(h.storage.state() == fixture::TxStorage::State::DriverBorrow);
	CHECK(std::equal(copy.begin(), copy.begin() + packet.bytes.size(),
	                 h.storage.packet().bytes.begin()));
	CHECK(h.storage.requestAbort());
	CHECK(!h.storage.driverComplete(h.storage.lease()));
	CHECK(h.storage.abortComplete(h.storage.lease()));
	CHECK(!h.client.confirmSessionClosed(2));
	CHECK(
	    h.client.confirmSessionClosed(1)); // Test peer and driver are explicitly excluded/drained.
	CHECK(h.client.admitDescriptor(fixture::Policy::observedHeader, 2) == tc::Status::Ok);
	CHECK(!h.client.nextPacket() && rpc_fixture::device.commands == 0);
}

int main()
{
	repeatedExecutionIsExplicit();
	lateReplyStopsRetry(false);
	lateReplyStopsRetry(true);
	retryFreshGate(false);
	retryFreshGate(true);
	timeoutRetainsBorrow();
	std::printf("transport_dma_retry checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
