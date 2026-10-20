/* Observable retransmission, actual Model effects, deadlines and typed callbacks. MIT. */
#include "RetryingFixture.hpp"
#include <cstdio>
#include <cstring>

namespace rf = retrying_fixture;
namespace ef = encoded_fixture;
namespace cl = telemetry::client;
namespace ie = telemetry::integration::encoded;
static unsigned checks = 0, failures = 0;

static void check(bool condition, unsigned line) noexcept
{
	++checks;
	if (!condition) {
		++failures;
		std::fprintf(stderr, "Retrying.cpp:%u failed (%u)\n", line, checks);
	}
}

#define CHECK(value) check(bool(value), __LINE__)

struct Harness {
	std::uint32_t now;
	std::array<std::byte, 128> tx{}, result{}, port{}, reply{}, observed{};
	std::array<std::byte, 1024> scratch{};
	telemetry::Workspace workspace{scratch};
	cl::Clock clock{this, +[](void* p) noexcept {
		                return static_cast<Harness*>(p)->now;
	                }};
	ie::RetryingEncodedBackend backend;
	rf::Client client{backend, clock};
	unsigned callbacks = 0, sends = 0;
	cl::ResultView last{};
	std::size_t replyBytes = 0;

	explicit Harness(ie::EncodedRetryOptions options = {}, std::uint32_t start = 0) noexcept
	    : now(start), backend(tx, result, clock, options)
	{
		ef::device = {};
		ef::optionalOwner.reset();
		const telemetry::descriptor::Descriptor descriptor{ef::model};
		CHECK(descriptor.read(0, rf::Policy::header).written == 64);
		CHECK(client.admitDescriptor(rf::Policy::header, 17) == cl::Status::Ok);
	}

	~Harness()
	{
		client.close();
		for (unsigned i = 0; i < 8 && !client.drained(); ++i)
			client.process(now);
		CHECK(client.drained());
	}

	void record(const cl::ResultView& outcome) noexcept
	{
		++callbacks;
		last = outcome;
		std::copy(outcome.payload.begin(), outcome.payload.end(), observed.begin());
		last.payload = cl::Input{observed}.first(outcome.payload.size());
	}

	cl::Submission enqueue(cl::Operation operation, std::uint32_t timeout = 1000,
	                       std::uint32_t endpoint = 0) noexcept
	{
		const auto payload = ef::configBytes(731, false);
		const bool read = operation == cl::Operation::FieldRead;
		const bool response = read || operation == cl::Operation::Service;
		return client.submitEncoded(
		    {operation, endpoint, read ? cl::Input{} : cl::Input{payload}, response ? 5u : 0u},
		    [this](const cl::ResultView& outcome) noexcept {
			    record(outcome);
		    },
		    timeout);
	}

	cl::Packet copy() noexcept
	{
		const auto offered = client.nextPacket();
		CHECK(offered && offered.bytes.size() <= port.size());
		if (!offered || offered.bytes.size() > port.size())
			return {};
		std::copy(offered.bytes.begin(), offered.bytes.end(), port.begin());
		return {cl::Input{port}.first(offered.bytes.size()), offered.session, offered.requestId};
	}

	void handoff(cl::Packet packet) noexcept
	{
		CHECK(client.prepareHandoff(packet.session, packet.requestId));
		CHECK(!client.prepareHandoff(packet.session, packet.requestId));
		CHECK(client.onSendAccepted(packet.session, packet.requestId));
		CHECK(!client.onSendAccepted(packet.session, packet.requestId));
		++sends;
	}

	void dispatch(cl::Packet packet) noexcept
	{
		const auto built =
		    ie::dispatchEncoded(ef::modelView, workspace, backend.session(), packet.bytes, reply);
		CHECK(built);
		replyBytes = built.written;
		CHECK(ef::wire::parse(cl::Input{reply}.first(replyBytes)));
	}

	void deliver(cl::Packet packet) noexcept
	{
		CHECK(client.onReply(cl::Input{reply}.first(replyBytes), packet.session, packet.requestId));
	}
};

static void typedRoundtrips()
{
	Harness h;
	rf::Config value{915, false};
	CHECK(h.client.write(rf::Field{}, value, [&](const auto& r) noexcept {
		h.record(r);
	}));
	value = {1, true}; // Queue and every retry must retain the encoded snapshot.
	auto packet = h.copy();
	CHECK(ef::wire::parse(packet.bytes).payload[0] == std::byte{0x93});
	h.handoff(packet);
	h.dispatch(packet);
	h.deliver(packet);
	CHECK(h.callbacks == 1 && h.last.succeeded() && ef::device.current.value == 915);
	CHECK(h.client.read(rf::Field{}, [&](const auto& r) noexcept {
		CHECK(r.hasValue() && r.value().value == 915 && !r.value().enabled);
		h.record(r);
	}));
	packet = h.copy();
	h.handoff(packet);
	h.dispatch(packet);
	h.deliver(packet);
	CHECK(h.callbacks == 2 && h.last.payload.size() == 5);
	ef::device.commandStatus = telemetry::CommandResult::Accepted;
	CHECK(h.client.command(rf::Command{}, value, [&](const auto& r) noexcept {
		CHECK(r.accepted() && !r.executed());
		h.record(r);
	}));
	packet = h.copy();
	h.handoff(packet);
	h.dispatch(packet);
	h.deliver(packet);
	CHECK(h.client.service(rf::Service{}, value, [&](const auto& r) noexcept {
		CHECK(r.hasValue() && r.value().value == 1 && r.value().enabled);
		h.record(r);
	}));
	packet = h.copy();
	h.handoff(packet);
	h.dispatch(packet);
	h.deliver(packet);
	CHECK(h.callbacks == 4 && h.sends == 4 && !h.backend.recoveryRequired());
	CHECK(ef::device.reads == 1 && ef::device.writes == 1 && ef::device.commands == 1 &&
	      ef::device.services == 1);
}

static void lossAndDuplicates()
{
	for (const auto operation : {cl::Operation::FieldWrite, cl::Operation::Command,
	                             cl::Operation::Service, cl::Operation::FieldRead}) {
		for (bool lostReply : {false, true}) {
			Harness h;
			CHECK(h.enqueue(operation));
			const auto first = h.copy();
			std::array<std::byte, 128> snapshot{};
			std::copy(first.bytes.begin(), first.bytes.end(), snapshot.begin());
			h.handoff(first);
			if (lostReply)
				h.dispatch(first); // Otherwise the peer never received it.
			h.now = 99;
			h.client.process(h.now);
			CHECK(!h.client.nextPacket() && h.callbacks == 0);
			h.now = 100;
			h.client.process(h.now);
			const auto retry = h.copy();
			CHECK(retry.session == first.session && retry.requestId == first.requestId);
			CHECK(retry.bytes.size() == first.bytes.size() &&
			      std::equal(retry.bytes.begin(), retry.bytes.end(), snapshot.begin()));
			CHECK(!h.client.onSendAccepted(retry.session, retry.requestId)); // New gate mandatory.
			h.handoff(retry);
			h.dispatch(retry);
			h.deliver(retry);
			CHECK(h.callbacks == 1 && h.last.succeeded() && h.sends == 2);
			CHECK(ef::calls() == (lostReply ? 2u : 1u)); // Deliberately no server dedup.
			CHECK(!h.client.recoveryRequired() && !h.client.nextPacket());
		}
	}
}

static void retryBudgets()
{
	for (const auto retries : {0u, 3u}) {
		for (const auto operation : {cl::Operation::Command, cl::Operation::FieldRead}) {
			Harness h{{100, retries}};
			CHECK(h.enqueue(operation));
			for (std::uint32_t attempt = 0; attempt <= retries; ++attempt) {
				h.now = 100 * attempt;
				const auto packet = h.copy();
				h.handoff(packet);
				CHECK(h.callbacks == 0);
			}
			h.now = 100 * (retries + 1);
			h.client.process(h.now);
			CHECK(h.callbacks == 1 && h.sends == retries + 1 && !h.client.nextPacket());
			CHECK(h.last.status == (operation == cl::Operation::FieldRead
			                            ? cl::Status::Timeout
			                            : cl::Status::OutcomeUnknown));
			CHECK(h.client.recoveryRequired() && ef::calls() == 0);
		}
	}
}

static void lateReplyWins()
{
	for (const bool prepareRetry : {false, true}) {
		Harness h;
		CHECK(h.enqueue(cl::Operation::Service));
		const auto first = h.copy();
		h.handoff(first);
		h.dispatch(first);
		h.now = 100;
		h.client.process(h.now);
		const auto retry = h.copy();
		if (prepareRetry)
			CHECK(h.client.prepareHandoff(retry.session, retry.requestId));
		h.deliver(first);
		CHECK(h.callbacks == 1 && h.last.succeeded() && ef::device.services == 1);
		CHECK(!h.client.nextPacket());
		CHECK(!h.client.prepareHandoff(retry.session, retry.requestId));
		CHECK(!h.client.onSendAccepted(retry.session, retry.requestId));
		CHECK(!h.client.recoveryRequired());
	}
}

static void cancellationAndRecovery()
{
	for (const auto operation : {cl::Operation::Command, cl::Operation::FieldRead}) {
		for (unsigned stage = 0; stage < 5; ++stage) {
			Harness h;
			const auto submitted = h.enqueue(operation);
			CHECK(submitted);
			const auto first = h.copy();
			if (stage >= 1)
				CHECK(h.client.prepareHandoff(first.session, first.requestId));
			if (stage >= 2)
				CHECK(h.client.onSendAccepted(first.session, first.requestId));
			if (stage >= 3) {
				h.now = 100;
				h.client.process(h.now);
				CHECK(h.client.nextPacket());
			}
			if (stage == 4)
				CHECK(h.client.prepareHandoff(first.session, first.requestId));
			CHECK(h.client.cancel(submitted.handle));
			h.client.process(h.now);
			CHECK(h.callbacks == 1 &&
			      h.last.status == (stage && operation != cl::Operation::FieldRead
			                            ? cl::Status::OutcomeUnknown
			                            : cl::Status::Cancelled));
			CHECK(h.client.recoveryRequired() == (stage != 0));
			h.now += 500;
			h.client.process(h.now);
			CHECK(!h.client.nextPacket() &&
			      !h.client.onSendAccepted(first.session, first.requestId));
			if (stage) {
				CHECK(h.client.admitDescriptor(rf::Policy::header, 18) ==
				      cl::Status::RecoveryRequired);
				CHECK(!h.client.confirmSessionClosed(18));
				CHECK(h.client.confirmSessionClosed(17)); // Test port/peer are now drained.
				CHECK(!h.backend.ready() && !h.client.nextPacket());
				CHECK(h.client.admitDescriptor(rf::Policy::header, 18) == cl::Status::Ok);
				CHECK(!h.client.nextPacket()); // No automatic replay.
				CHECK(h.enqueue(cl::Operation::Service));
				const auto fresh = h.copy();
				h.handoff(fresh);
				h.dispatch(fresh);
				CHECK(!h.client.onReply(cl::Input{h.reply}.first(h.replyBytes), first.session,
				                        first.requestId));
				h.deliver(fresh);
				CHECK(h.callbacks == 2 && h.last.succeeded());
			}
		}
	}
}

static void deadlinesAndWrap()
{
	{ // The overall queue deadline expires before any initial handoff.
		Harness h;
		CHECK(h.enqueue(cl::Operation::Command, 10));
		h.now = 9;
		const auto packet = h.copy();
		h.now = 10;
		CHECK(!h.client.prepareHandoff(packet.session, packet.requestId));
		CHECK(h.callbacks == 1 && h.last.status == cl::Status::Timeout);
		CHECK(!h.client.recoveryRequired() && ef::calls() == 0);
	}
	{ // Reply timeout starts at acceptance, not prepareHandoff.
		Harness h{{10, 1}};
		CHECK(h.enqueue(cl::Operation::Command, 250));
		const auto packet = h.copy();
		CHECK(h.client.prepareHandoff(packet.session, packet.requestId));
		h.now = 100;
		h.client.process(h.now);
		CHECK(!h.client.nextPacket() && !h.callbacks);
		CHECK(h.client.onSendAccepted(packet.session, packet.requestId));
		h.now = 109;
		CHECK(!h.client.nextPacket());
		h.now = 110;
		CHECK(h.client.nextPacket());
	}
	{ // Overall deadline truncates the last attempt; no retry budget can extend it.
		Harness h{{100, 3}};
		CHECK(h.enqueue(cl::Operation::Command, 150));
		auto packet = h.copy();
		h.handoff(packet);
		h.now = 100;
		packet = h.copy();
		h.handoff(packet);
		h.now = 150;
		h.client.process(h.now);
		CHECK(h.callbacks == 1 && h.last.status == cl::Status::OutcomeUnknown);
		CHECK(!h.client.nextPacket() && h.client.recoveryRequired());
	}
	{ // A copied retry cannot pass its gate after the overall deadline.
		Harness h;
		CHECK(h.enqueue(cl::Operation::Command, 120));
		auto packet = h.copy();
		h.handoff(packet);
		h.now = 100;
		packet = h.copy();
		h.now = 120;
		CHECK(!h.client.prepareHandoff(packet.session, packet.requestId));
		CHECK(h.callbacks == 1 && h.last.status == cl::Status::OutcomeUnknown);
		CHECK(h.client.recoveryRequired());
	}
	{ // There is no inherited hidden 30-second backend timeout.
		Harness h{{35000, 0}};
		CHECK(h.enqueue(cl::Operation::FieldRead, 40000));
		const auto packet = h.copy();
		h.handoff(packet);
		h.now = 30000;
		h.client.process(h.now);
		CHECK(!h.callbacks);
		h.now = 34999;
		h.client.process(h.now);
		CHECK(!h.callbacks);
		h.now = 35000;
		h.client.process(h.now);
		CHECK(h.callbacks == 1 && h.last.status == cl::Status::Timeout);
	}
	{ // Both reply and overall arithmetic cross modulo-u32 wrap.
		Harness h{{50, 1}, UINT32_MAX - 40};
		CHECK(h.enqueue(cl::Operation::FieldRead, 120));
		auto packet = h.copy();
		h.handoff(packet);
		h.now = 8;
		CHECK(!h.client.nextPacket());
		h.now = 9;
		packet = h.copy();
		h.handoff(packet);
		h.now = 58;
		CHECK(!h.client.nextPacket());
		h.now = 59;
		h.client.process(h.now);
		CHECK(h.callbacks == 1 && h.last.status == cl::Status::Timeout);
		CHECK(h.client.recoveryRequired());
	}
}

static void finalReplyBoundary()
{
	for (const auto operation : {cl::Operation::Command, cl::Operation::FieldRead}) {
		for (const bool beforeDeadline : {false, true}) {
			Harness h{{100, 0}};
			CHECK(h.enqueue(operation));
			const auto packet = h.copy();
			h.handoff(packet);
			h.dispatch(packet);
			h.now = beforeDeadline ? 99 : 100;
			// DeviceClient processes timers before handing a frame to Backend.
			CHECK(h.client.onReply(cl::Input{h.reply}.first(h.replyBytes), packet.session,
			                       packet.requestId) == beforeDeadline);
			CHECK(h.callbacks == 1);
			CHECK(h.last.status == (beforeDeadline ? cl::Status::Ok
			                        : operation == cl::Operation::FieldRead
			                            ? cl::Status::Timeout
			                            : cl::Status::OutcomeUnknown));
			CHECK(h.client.recoveryRequired() != beforeDeadline);
		}
	}
}

static void refusalsMalformedAndCorrelation()
{
	for (const auto endpoint : {2u, 0x10000u}) {
		Harness h;
		CHECK(h.enqueue(cl::Operation::Service, 1000, endpoint));
		const auto packet = h.copy();
		h.handoff(packet);
		h.dispatch(packet);
		h.deliver(packet);
		CHECK(h.callbacks == 1 && !h.last.succeeded() && h.last.status == cl::Status::Ok);
		CHECK(endpoint == 2 ? h.last.endpointStatus == 3 : h.last.dispatch == 4);
		h.now = 100;
		h.client.process(h.now);
		CHECK(!h.client.nextPacket() && !h.client.recoveryRequired());
	}
	for (const auto operation : {cl::Operation::Command, cl::Operation::FieldRead}) {
		Harness h;
		CHECK(h.enqueue(operation));
		const auto packet = h.copy();
		h.handoff(packet);
		h.dispatch(packet);
		CHECK(!h.client.onReply(cl::Input{h.reply}.first(3), 18, packet.requestId));
		for (auto offset : {8u, 12u, 16u}) {
			auto wrong = h.reply;
			ef::put32(wrong, offset, offset == 8 ? 18 : 999);
			CHECK(!h.client.onReply(cl::Input{wrong}.first(h.replyBytes), 17, packet.requestId));
		}
		auto wrongOperation = h.reply;
		wrongOperation[6] = operation == cl::Operation::Command ? std::byte{1} : std::byte{3};
		CHECK(ef::wire::parse(cl::Input{wrongOperation}.first(h.replyBytes)));
		CHECK(
		    !h.client.onReply(cl::Input{wrongOperation}.first(h.replyBytes), 17, packet.requestId));
		CHECK(h.callbacks == 0);
		h.reply[0] = std::byte{0};
		h.deliver(packet);
		CHECK(h.callbacks == 1 && h.last.status == (operation == cl::Operation::FieldRead
		                                                ? cl::Status::MalformedReply
		                                                : cl::Status::OutcomeUnknown));
		CHECK(h.client.recoveryRequired() && !h.client.nextPacket());
	}
}

int main(int argc, char** argv)
{
#if defined(RETRYING_NO_HEAP)
	if (argc == 2 && std::strcmp(argv[1], "--allocation-control") == 0)
		(void)::operator new(1);
#endif
	if (argc == 2 && (std::strcmp(argv[1], "--zero-reply-timeout") == 0 ||
	                  std::strcmp(argv[1], "--oversized-reply-timeout") == 0)) {
		std::fputs("RETRY_INVALID_OPTIONS_CONTROL\n", stderr);
		Harness invalid{{argv[1][2] == 'z' ? 0u : cl::maxTimeoutMs + 1, 3}};
		return 1;
	}
	static_assert(ie::EncodedRetryOptions{}.replyTimeoutMs == 100);
	static_assert(ie::EncodedRetryOptions{}.maxRetries == 3);
	static_assert(!std::is_copy_constructible_v<ie::RetryingEncodedBackend>);
	static_assert(!std::is_move_constructible_v<ie::RetryingEncodedBackend>);
	typedRoundtrips();
	lossAndDuplicates();
	retryBudgets();
	lateReplyWins();
	cancellationAndRecovery();
	deadlinesAndWrap();
	finalReplyBoundary();
	refusalsMalformedAndCorrelation();
	std::printf("retrying checks=%u failures=%u\n", checks, failures);
	std::printf("retrying backend_bytes plain=%zu retrying=%zu\n", sizeof(ie::EncodedBackend),
	            sizeof(ie::RetryingEncodedBackend));
	return failures ? 1 : 0;
}
