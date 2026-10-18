/* Counted session isolation, ACK ordering, lifecycle and dispatch checks. MIT. */
#include "Fixture.hpp"
#include <cstdio>

using namespace rpc_sessions_fixture;
static unsigned checks = 0, failures = 0;

static void check(bool condition, unsigned line) noexcept
{
	++checks;
	if (!condition) {
		++failures;
		std::fprintf(stderr, "rpc sessions Check.cpp:%u failed (%u)\n", line, checks);
	}
}

#define CHECK(condition) check(bool(condition), __LINE__)

template<std::size_t N>
static auto readRecord(Harness<N>& h, rpc::RpcSessionHandle session,
                       std::array<std::byte, 128>& output) noexcept
{
	std::array<std::byte, 13> packet{};
	CHECK(pc::makeRead(packet, 0, 0).status == pc::BuildStatus::Ok);
	const auto read = h.pool.handle(session, packet, output);
	CHECK(read.status == Status::Ok && read.ticket.serial == 0);
	CHECK(read.reply.status == resource::Status::Ok);
	CHECK(read.reply.written >= resource::protocol::wire::chunkReplyHeaderSize);
	const auto parsed = pc::parseRead(Input{output}.first(read.reply.written));
	CHECK(parsed && !parsed.shortError && parsed.response.status == resource::Status::Ok);
	CHECK(parsed.response.data.size() >= rpc::rpcReplyHeaderBytes);
	return parsed.response;
}

template<std::size_t N>
static void admission() noexcept
{
	Harness<N> h;
	std::array<rpc::RpcSessionHandle, N> sessions{};
	std::array<std::byte, 128> output{};
	CHECK(!h.pool.valid({}));
	CHECK(h.pool.release({}) == Status::InvalidSession);
	CHECK(h.pool.replyAccepted({}, {}) == Status::InvalidSession);
	CHECK(h.pool.handle({}, {}, output).status == Status::InvalidSession);
	for (std::size_t i = 0; i < N; ++i) {
		const rpc::RpcConnectionKey key{static_cast<std::uint32_t>(i + 10), 1, 0};
		const auto acquired = h.pool.acquire(key);
		CHECK(acquired.status == Status::Ok);
		CHECK(acquired.session.index == i && acquired.session.generation == 1);
		CHECK(acquired.session.connection == key);
		CHECK(h.pool.valid(acquired.session));
		sessions[i] = acquired.session;
		const auto existing = h.pool.acquire(key);
		CHECK(existing.status == Status::Existing && existing.session == sessions[i]);
	}
	CHECK(h.pool.acquire({999, 1, 0}).status == Status::NoCapacity);
	for (const auto session : sessions)
		CHECK(h.pool.valid(session));
	auto changed = sessions[0];
	++changed.connection.peer;
	CHECK(!h.pool.valid(changed));
	changed = sessions[0];
	++changed.connection.generation;
	CHECK(!h.pool.valid(changed));
	changed = sessions[0];
	++changed.connection.channel;
	CHECK(!h.pool.valid(changed));
	changed = sessions[0];
	++changed.generation;
	CHECK(!h.pool.valid(changed));
	changed = sessions[0];
	changed.index = N;
	CHECK(!h.pool.valid(changed));
	changed = sessions[0];
	changed.owner = nullptr;
	CHECK(!h.pool.valid(changed));
	CHECK(h.pool.release(changed) == Status::InvalidSession);
	CHECK(h.pool.process(1, 0).dispatched == 0);
	CHECK(h.pool.process(2, SIZE_MAX).visited == N);
	Harness<1> other;
	const auto foreign = other.pool.acquire(sessions[0].connection).session;
	CHECK(!h.pool.valid(foreign) && !other.pool.valid(sessions[0]));
	CHECK(h.pool.handle(foreign, {}, output).status == Status::InvalidSession);
	CHECK(h.pool.release(foreign) == Status::InvalidSession);
	for (const auto session : sessions) {
		CHECK(h.pool.release(session) == Status::Ok);
		CHECK(!h.pool.valid(session));
		CHECK(h.pool.release(session) == Status::InvalidSession);
	}
	const auto reused = h.pool.acquire({0, 0, 0});
	CHECK(reused.status == Status::Ok && reused.session.index == 0);
	CHECK(reused.session.generation == 2 && !h.pool.valid(sessions[0]));
	CHECK(h.a.mailbox.state() == rpc::RpcState::Idle);
}

template<std::size_t N>
static void independentTransactions() noexcept
{
	first = Counter{};
	Harness<N> h;
	std::array<rpc::RpcSessionHandle, N> sessions{};
	std::array<rpc::RpcReplyTicket, N> tickets{};
	std::array<std::byte, 128> output{};
	for (std::size_t i = 0; i < N; ++i) {
		sessions[i] = h.pool.acquire({1, 1, static_cast<std::uint32_t>(i)}).session;
		const auto bytes = request(static_cast<std::uint32_t>(i + 1)); // Every slot uses tx=1.
		const auto ack = submit(h.pool, sessions[i], bytes, output);
		CHECK(ack.status == Status::Ok && ack.reply.status == resource::Status::Ok);
		CHECK(ack.reply.written == resource::protocol::wire::writeReplySize);
		CHECK(ack.ticket.serial != 0 && ack.ticket.index == i);
		const auto parsed = pc::parseWrite(Input{output}.first(ack.reply.written), bytes.size());
		CHECK(parsed && parsed.response.complete && parsed.response.consumed == bytes.size());
		CHECK(first.calls == 0 && h.slot(i).mailbox.state() == rpc::RpcState::Pending);
		tickets[i] = ack.ticket;
	}
	CHECK(h.pool.process(100, SIZE_MAX).dispatched == 0 && first.calls == 0);
	for (std::size_t i = 0; i < N; ++i) {
		const auto record = readRecord(h, sessions[i], output);
		CHECK(record.data[21] == std::byte(rpc::RpcState::Pending));
		CHECK(h.pool.replyAccepted(sessions[i], {}) == Status::StaleReply);
		if (N > 1)
			CHECK(h.pool.replyAccepted(sessions[i], tickets[(i + 1) % N]) == Status::StaleReply);
	}
	CHECK(h.pool.process(101, SIZE_MAX).dispatched == 0);
	for (std::size_t i = 0; i < N; ++i) {
		CHECK(h.pool.replyAccepted(sessions[i], tickets[i]) == Status::Ok);
		CHECK(h.pool.replyAccepted(sessions[i], tickets[i]) == Status::StaleReply);
	}
	CHECK(h.pool.process(102, 0).dispatched == 0 && first.calls == 0);
	const auto firstStep = h.pool.process(103);
	CHECK(firstStep.status == Status::Ok && firstStep.dispatched == 1);
	CHECK(first.calls == 1 && first.value == 1);
	CHECK(h.pool.process(104, SIZE_MAX).dispatched == N - 1);
	CHECK(first.calls == N && first.value == N * (N + 1) / 2 && first.maxDepth == 1);
	std::uint32_t accumulated = 0;
	for (std::size_t i = 0; i < N; ++i) {
		accumulated += static_cast<std::uint32_t>(i + 1);
		const auto record = readRecord(h, sessions[i], output);
		CHECK(record.data[21] == std::byte(rpc::RpcState::Ready));
		CHECK(record.data[22] == std::byte(ts::DispatchStatus::Ok));
		CHECK(record.data[23] == std::byte(ts::ServiceStatus::Ok));
		Report value{};
		CHECK(ts::decode(record.data.subspan(rpc::rpcReplyHeaderBytes), value) ==
		      ts::CodecStatus::Ok);
		CHECK(value.value == accumulated && record.eof);
		const auto retry =
		    submit(h.pool, sessions[i], request(static_cast<std::uint32_t>(i + 1)), output);
		CHECK(retry.reply.status == resource::Status::Ok && retry.ticket.serial == 0);
		CHECK(h.pool.process(105, SIZE_MAX).dispatched == 0 && first.calls == N);
		CHECK(h.slot(i).intact());
	}
	CHECK(h.pool.release(sessions[0]) == Status::Ok);
	const auto replacement = h.pool.acquire({500, 2, 0}).session;
	CHECK(replacement.index == 0 && replacement.generation != sessions[0].generation);
	CHECK(h.pool.replyAccepted(replacement, tickets[0]) == Status::StaleReply);
	CHECK(h.pool.handle(sessions[0], {}, output).status == Status::InvalidSession);
	CHECK(submit(h.pool, sessions[0], request(99), output).status == Status::InvalidSession);
	const auto newAck = submit(h.pool, replacement, request(10), output);
	CHECK(newAck.reply.status == resource::Status::Ok && newAck.ticket.serial != 0);
	CHECK(h.pool.replyAccepted(replacement, tickets[0]) == Status::StaleReply);
	for (std::size_t i = 1; i < N; ++i) {
		const auto record = readRecord(h, sessions[i], output);
		CHECK(record.data[21] == std::byte(rpc::RpcState::Ready));
	}
	CHECK(h.pool.replyAccepted(replacement, newAck.ticket) == Status::Ok);
	CHECK(h.pool.process(106, SIZE_MAX).dispatched == 1 && first.calls == N + 1);
	CHECK(h.a.intact());
}

static void acknowledgementGates() noexcept
{
	first = Counter{};
	Harness<2> h;
	const auto a = h.pool.acquire({1, 1, 0}).session;
	const auto b = h.pool.acquire({2, 1, 0}).session;
	std::array<std::byte, 128> out{};
	const auto bytes = request(7);
	CHECK(h.pool.process(1).dispatched == 0);
	const auto shortAck = submit(h.pool, a, bytes, Output{out}.first(13));
	CHECK(shortAck.reply.status == resource::Status::BufferTooSmall && shortAck.ticket.serial == 0);
	CHECK(h.a.mailbox.state() == rpc::RpcState::Idle);
	const auto firstAck = submit(h.pool, a, bytes, out);
	CHECK(firstAck.ticket.serial != 0 && first.calls == 0);
	CHECK(h.pool.process(2).dispatched == 0);
	const auto read = readRecord(h, a, out);
	CHECK(read.data[21] == std::byte(rpc::RpcState::Pending));
	CHECK(h.pool.process(3, SIZE_MAX).dispatched == 0);
	const std::array<std::byte, 1> extra{std::byte{0x44}};
	const auto otherFile = submit(h.pool, a, extra, out, 0, true, 1);
	CHECK(otherFile.reply.status == resource::Status::Ok && otherFile.ticket.serial == 0);
	CHECK(h.a.extra.writes == 1);
	CHECK(h.pool.replyAccepted(a, otherFile.ticket) == Status::StaleReply);
	CHECK(h.pool.process(4).dispatched == 0);
	const auto failed = submit(h.pool, a, request(8), out);
	CHECK(failed.reply.status == resource::Status::InvalidData && failed.ticket.serial == 0);
	CHECK(h.pool.replyAccepted(a, failed.ticket) == Status::StaleReply);
	const auto retryAck = submit(h.pool, a, bytes, out);
	CHECK(retryAck.ticket.serial != firstAck.ticket.serial);
	CHECK(h.pool.replyAccepted(a, firstAck.ticket) == Status::StaleReply);
	const auto bAck = submit(h.pool, b, request(20), out);
	CHECK(h.pool.replyAccepted(a, bAck.ticket) == Status::StaleReply);
	Harness<1> foreign;
	const auto f = foreign.pool.acquire({1, 1, 0}).session;
	const auto fAck = submit(foreign.pool, f, request(1), out);
	CHECK(h.pool.replyAccepted(a, fAck.ticket) == Status::StaleReply);
	CHECK(h.pool.replyAccepted(a, retryAck.ticket) == Status::Ok);
	CHECK(h.pool.process(5).dispatched == 1 && first.calls == 1 && first.value == 7);
	CHECK(h.pool.process(6, SIZE_MAX).dispatched == 0);
	CHECK(h.pool.replyAccepted(b, bAck.ticket) == Status::Ok);
	CHECK(h.pool.process(7).dispatched == 1 && first.value == 27 && first.calls == 2);
	// Resource explicitly permits a shared request/reply packet buffer.
	CHECK(h.pool.release(a) == Status::Ok);
	const auto current = h.pool.acquire({1, 2, 0}).session;
	auto packet = writePacket(request(5));
	const auto aliased =
	    h.pool.handle(current, Input{packet.first}.first(packet.second), packet.first);
	CHECK(aliased.reply.status == resource::Status::Ok && aliased.ticket.serial != 0);
	CHECK(h.pool.process(8).dispatched == 0);
	CHECK(h.pool.replyAccepted(current, aliased.ticket) == Status::Ok);
	CHECK(h.pool.process(9).dispatched == 1 && first.value == 32 && first.calls == 3);
}

static void fragmentedAndTimed() noexcept
{
	first = Counter{};
	Harness<2> h;
	const auto a = h.pool.acquire({1, 1, 0}).session;
	const auto b = h.pool.acquire({2, 1, 0}).session;
	std::array<std::byte, 128> output{};
	CHECK(h.pool.process(100).dispatched == 0);
	const auto held = submit(h.pool, a, request(1), output);
	CHECK(held.ticket.serial != 0);
	const auto bytes = request(3);
	const auto firstPart = submit(h.pool, b, Input{bytes}.first(24), output, 0, false);
	CHECK(firstPart.reply.status == resource::Status::Ok && firstPart.ticket.serial == 0);
	CHECK(h.b.mailbox.state() == rpc::RpcState::Receiving);
	CHECK(h.pool.process(100 + rpc::rpcReceiveTimeoutMs - 1, 0).dispatched == 0);
	CHECK(h.b.mailbox.state() == rpc::RpcState::Receiving);
	CHECK(h.pool.process(100 + rpc::rpcReceiveTimeoutMs, 0).dispatched == 0);
	CHECK(h.b.mailbox.state() == rpc::RpcState::Idle);
	CHECK(h.a.mailbox.state() == rpc::RpcState::Pending && first.calls == 0);
	CHECK(submit(h.pool, b, bytes, output).reply.status == resource::Status::CursorExpired);
	const auto bytes2 = request(3, 2);
	CHECK(submit(h.pool, b, Input{bytes2}.first(24), output, 0, false).reply.status ==
	      resource::Status::Ok);
	const auto finalPart =
	    submit(h.pool, b, Input{bytes2}.subspan(24), output, rpc::rpcCursor(2, 24), true);
	CHECK(finalPart.ticket.serial != 0 && h.b.mailbox.state() == rpc::RpcState::Pending);
	CHECK(h.pool.process(20000, SIZE_MAX).dispatched == 0);
	CHECK(h.pool.replyAccepted(b, finalPart.ticket) == Status::Ok);
	CHECK(h.pool.process(20001).dispatched == 1 && first.calls == 1 && first.value == 3);
	CHECK(h.pool.replyAccepted(a, held.ticket) == Status::Ok);
	CHECK(h.pool.process(20002).dispatched == 1 && first.value == 4);
	CHECK(h.pool.release(b) == Status::Ok);
	const auto b2 = h.pool.acquire({2, 2, 0}).session;
	CHECK(h.pool.process(UINT32_MAX - 100).dispatched == 0);
	CHECK(submit(h.pool, b2, Input{bytes}.first(24), output, 0, false).reply.status ==
	      resource::Status::Ok);
	CHECK(h.pool.process(rpc::rpcReceiveTimeoutMs - 102).dispatched == 0);
	CHECK(h.b.mailbox.state() == rpc::RpcState::Receiving);
	CHECK(h.pool.process(rpc::rpcReceiveTimeoutMs - 101).dispatched == 0);
	CHECK(h.b.mailbox.state() == rpc::RpcState::Idle);
}

static Harness<2>* runningHarness = nullptr;
static rpc::RpcSessionHandle runningSession{}, otherSession{};

static void duringCallback() noexcept
{
	auto& h = *runningHarness;
	CHECK(h.a.mailbox.state() == rpc::RpcState::Running);
	CHECK(h.pool.process(9, 2).status == Status::Reentrant);
	CHECK(h.pool.acquire({99, 1, 0}).status == Status::Reentrant);
	std::array<std::byte, 16> output{};
	CHECK(h.pool.handle(otherSession, {}, output).status == Status::Reentrant);
	CHECK(h.pool.release(runningSession) == Status::Closing);
	CHECK(!h.pool.valid(runningSession));
	CHECK(h.a.mailbox.state() == rpc::RpcState::Running);
	CHECK(h.pool.release(runningSession) == Status::InvalidSession);
	CHECK(h.pool.acquire({99, 2, 0}).status == Status::Reentrant);
	CHECK(first.depth == 1 && first.calls == 1);
}

static void reentrancyAndModels() noexcept
{
	first = Counter{};
	second = Counter{};
	Harness<2> h{true};
	runningHarness = &h;
	runningSession = h.pool.acquire({1, 1, 0}).session;
	otherSession = h.pool.acquire({2, 1, 0}).session;
	std::array<std::byte, 128> output{};
	const auto a = submit(h.pool, runningSession, request(7), output);
	const auto b = submit(h.pool, otherSession, request(20), output);
	CHECK(h.pool.replyAccepted(runningSession, a.ticket) == Status::Ok);
	CHECK(h.pool.replyAccepted(otherSession, b.ticket) == Status::Ok);
	first.hook = duringCallback;
	const auto stepped = h.pool.process(1, SIZE_MAX);
	first.hook = nullptr;
	CHECK(stepped.status == Status::Ok && stepped.dispatched == 2);
	CHECK(first.calls == 1 && second.calls == 1 && first.maxDepth == 1 && second.maxDepth == 1);
	CHECK(first.value == 7 && second.value == 20);
	CHECK(h.a.mailbox.state() == rpc::RpcState::Idle);
	CHECK(h.b.mailbox.state() == rpc::RpcState::Ready);
	CHECK(h.pool.valid(otherSession));
	const auto record = readRecord(h, otherSession, output);
	Report report{};
	CHECK(ts::decode(record.data.subspan(rpc::rpcReplyHeaderBytes), report) == ts::CodecStatus::Ok);
	CHECK(report.value == 20);
	const auto reused = h.pool.acquire({1, 2, 0});
	CHECK(reused.status == Status::Ok && reused.session.index == 0);
	CHECK(!h.pool.valid(runningSession) && h.pool.valid(reused.session));
	CHECK(h.pool.replyAccepted(reused.session, a.ticket) == Status::StaleReply);
	CHECK(h.a.intact() && h.b.intact());
	runningHarness = nullptr;
}

int main()
{
	admission<1>();
	admission<2>();
	admission<4>();
	independentTransactions<1>();
	independentTransactions<2>();
	independentTransactions<4>();
	acknowledgementGates();
	fragmentedAndTimed();
	reentrancyAndModels();
	std::printf("rpc sessions checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
