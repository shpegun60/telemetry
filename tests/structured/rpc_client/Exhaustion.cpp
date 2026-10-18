/*
 * @file Exhaustion.cpp
 * @brief Single-TU boundary controls for finite client generation/packet IDs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Dependencies are parsed first, then only this client's access specifier is
 * exposed. Its real compiled implementation is included in this same TU: do
 * not also link RpcClient.cpp. There is one client class definition, avoiding
 * different access-token definitions across translation units. No production
 * testing hook, shortened counter or simulated state machine is introduced.
 */
#include <resource/telemetry/v3/RpcWire.hpp>
#include <telemetry/codec/Codec.hpp>
#include <telemetry/result/Dispatch.hpp>
#include <telemetry/result/EndpointStatus.hpp>
#include <cstdlib>
#include <type_traits>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include <resource/telemetry/v3/RpcClient.hpp>
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <resource/telemetry/v3/RpcClient.cpp>
#include <array>
#include <cstdio>

namespace rpc = resource::telemetry::v3;
using Status = rpc::RpcClientStatus;
static unsigned checks = 0, failures = 0;

static void check(bool condition) noexcept
{
	++checks;
	if (!condition)
		++failures;
}

int main()
{
	std::array<std::byte, 24> request{};
	std::array<std::byte, 1> result{};
	std::array<std::byte, 40> packet{};
	rpc::RpcClient client{0, request, result, packet};
	constexpr rpc::CommandEndpoint<> command{0, rpc::trustedRpcMapping};
	check(client.startSession(1) == Status::Ok);
	client.ticket_ = UINT32_MAX;
	check(client.beginCommand(command) == Status::RequestIdsExhausted);
	check(client.state() == rpc::RpcClientState::Idle && client.ticket_ == UINT32_MAX);
	check(client.startSession(2) == Status::Ok && client.ticket_ == 0);
	client.ticket_ = UINT32_MAX - 1;
	check(client.beginCommand(command) == Status::Ok);
	const auto outgoing = client.nextPacket();
	check(outgoing.requestId == UINT32_MAX && outgoing.generation == 2);
	check(client.onSendAccepted(2, UINT32_MAX));
	std::array<std::byte, 14> ack{};
	resource::protocol::client::detail::store(resource::Output{ack}, 1, rpc::rpcCursor(1, 24));
	resource::protocol::client::detail::store(resource::Output{ack}, 9, std::uint32_t{24});
	ack[13] = std::byte{1};
	check(client.onReply(ack, 2, UINT32_MAX));
	check(!client.nextPacket());
	check(client.status() == Status::OutcomeUnknown &&
	      client.diagnostic().cause == Status::RequestIdsExhausted);
	check(client.ticket_ == UINT32_MAX);
	check(client.startSession(UINT32_MAX, UINT32_MAX) == Status::Ok);
	check(client.beginCommand(command) == Status::TransactionIdsExhausted);
	check(client.startSession(UINT32_MAX) == Status::InvalidGeneration);
	check(client.startSession(0) == Status::InvalidGeneration);
	check(client.generation_ == UINT32_MAX && client.transactionId_ == UINT32_MAX);
	std::printf("rpc_client checks=%u failures=%u\n", checks, failures);
	return failures != 0;
}
