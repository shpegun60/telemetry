/*
 * @file Arm.cpp
 * @brief Real client-only retained Cortex-M7 roots and large decode stack probe.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <resource/telemetry/v3/RpcClient.hpp>
#include <array>
namespace rpc = resource::telemetry::v3;

struct Request {
	std::uint32_t value;
};

struct Report {
	std::array<std::uint32_t, 1024> values;
};

static std::array<std::byte, 28> request;
static std::array<std::byte, 4096> result;
static std::array<std::byte, 64> packet;
static rpc::RpcClient client{0, request, result, packet};
static Report report;
static std::array<std::byte, 80> incoming;

#ifdef RPC_CLIENT_ARM_PROBE
extern "C" [[noreturn]] void abort()
{
	__builtin_trap();
}
#endif

extern "C" unsigned rpc_client_begin(unsigned value) noexcept
{
	(void)client.startSession(value);
	return unsigned(client.beginService(
	    rpc::ServiceEndpoint<Request, Report>{0, rpc::trustedRpcMapping}, Request{value}));
}

extern "C" unsigned rpc_client_packet() noexcept
{
	const auto outgoing = client.nextPacket();
	return unsigned(outgoing.bytes.size()) + outgoing.requestId;
}

extern "C" unsigned rpc_client_accepted(unsigned generation, unsigned ticket) noexcept
{
	return client.onSendAccepted(generation, ticket);
}

extern "C" unsigned rpc_client_reply(unsigned generation, unsigned ticket, unsigned count) noexcept
{
	return client.onReply(resource::Input{incoming}.first(count % 81), generation, ticket);
}

extern "C" void rpc_client_process(unsigned now) noexcept
{
	client.process(now);
}

extern "C" unsigned rpc_client_decode() noexcept
{
	return unsigned(client.decodeResult(report).status) + report.values.back();
}

extern "C" void rpc_client_reset(unsigned generation) noexcept
{
	client.loseSession(generation);
	client.reset();
}

extern "C" unsigned rpc_client_roots(unsigned operation) noexcept
{
	switch (operation) {
		case 0:
			return rpc_client_begin(1);
		case 1:
			return rpc_client_packet();
		case 2:
			return rpc_client_accepted(1, 1);
		case 3:
			return rpc_client_reply(1, 1, 14);
		case 4:
			rpc_client_process(100);
			return unsigned(client.state());
		case 5:
			return rpc_client_decode();
		default:
			rpc_client_reset(1);
			return unsigned(client.status());
	}
}
#ifdef RPC_CLIENT_FRAME_CONTROL
extern "C" __attribute__((noinline)) unsigned rpc_client_frame_control(unsigned value) noexcept
{
	volatile std::byte oversizedFrame[4096];
	for (unsigned i = 0; i < 4096; ++i)
		oversizedFrame[i] = std::byte(value + i);
	return std::to_integer<unsigned>(oversizedFrame[value & 4095]);
}
#endif
#ifdef ABI_MAIN
static volatile unsigned selectedOperation = 4;

int main()
{
	return int(rpc_client_roots(selectedOperation));
}
#endif
