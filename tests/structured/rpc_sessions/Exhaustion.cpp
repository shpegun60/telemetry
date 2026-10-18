/* Isolated access-control probe for the real u32 exhaustion branches. MIT. */
// Preload every dependency before changing access control in this one header.
// This executable has exactly one pool-definition TU; no mixed-definition ODR.
#include <integration/telemetry/v3/RpcMailbox.hpp>
#include <resource/protocol/Packets.hpp>
#include <resource/protocol/Protocol.hpp>
#include <array>
#include <limits>
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include <integration/telemetry/v3/RpcSessionPool.hpp>
#undef private
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "Fixture.hpp"
#include <cstdio>

using namespace rpc_sessions_fixture;
static unsigned checks = 0, failures = 0;

static void check(bool condition) noexcept
{
	++checks;
	if (!condition)
		++failures;
}

int main()
{
	Harness<1> generation;
	generation.pool.slots_[0].generation = UINT32_MAX - 1;
	const auto last = generation.pool.acquire({1, 1, 0});
	check(last.status == Status::Ok);
	check(last.session.generation == UINT32_MAX);
	check(generation.pool.valid(last.session));
	check(generation.pool.release(last.session) == Status::Ok);
	check(!generation.pool.valid(last.session));
	check(generation.pool.acquire({2, 1, 0}).status == Status::NoCapacity);
	check(generation.pool.slots_[0].state == decltype(generation.pool)::SlotState::Retired);
	check(generation.pool.slots_[0].generation == UINT32_MAX);
	Harness<1> alreadyExhausted;
	alreadyExhausted.pool.slots_[0].generation = UINT32_MAX;
	check(alreadyExhausted.pool.acquire({1, 1, 0}).status == Status::NoCapacity);
	check(alreadyExhausted.pool.slots_[0].state ==
	      decltype(alreadyExhausted.pool)::SlotState::Retired);
	Harness<1> serial;
	const auto session = serial.pool.acquire({1, 1, 0}).session;
	serial.pool.slots_[0].serial = UINT32_MAX - 1;
	std::array<std::byte, 128> output{};
	const auto ack = submit(serial.pool, session, request(1), output);
	check(ack.status == Status::Ok);
	check(ack.reply.status == resource::Status::Ok);
	check(ack.ticket.serial == UINT32_MAX);
	check(serial.pool.slots_[0].serial == UINT32_MAX);
	check(serial.pool.replyAccepted(session, ack.ticket) == Status::Ok);
	check(serial.pool.process(1).dispatched == 1);
	std::array<std::byte, 13> read{};
	check(pc::makeRead(read, 0, 0).status == pc::BuildStatus::Ok);
	const auto result = serial.pool.handle(session, read, output);
	check(result.status == Status::Ok && result.reply.status == resource::Status::Ok);
	check(result.ticket.serial == 0);
	const auto exhausted = submit(serial.pool, session, request(1, 2), output);
	check(exhausted.status == Status::Exhausted && exhausted.reply.written == 0);
	check(serial.a.mailbox.state() == ::telemetry::protocol::rpc::RpcState::Ready);
	check(serial.pool.process(2).dispatched == 0);
	check(serial.pool.replyAccepted(session, ack.ticket) == Status::StaleReply);
	check(serial.pool.release(session) == Status::Ok);
	check(serial.pool.slots_[0].state == decltype(serial.pool)::SlotState::Retired);
	check(serial.pool.acquire({2, 1, 0}).status == Status::NoCapacity);
	Harness<1> serialFree;
	serialFree.pool.slots_[0].serial = UINT32_MAX;
	check(serialFree.pool.acquire({1, 1, 0}).status == Status::NoCapacity);
	check(serialFree.pool.slots_[0].state == decltype(serialFree.pool)::SlotState::Retired);
	std::printf("rpc sessions exhaustion checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
