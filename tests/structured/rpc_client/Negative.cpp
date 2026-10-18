/*
 * @file Negative.cpp
 * @brief Intended compile failures for client lifetime and exact typed API.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/client/rpc/RpcClient.hpp>
#include <array>
#include <utility>
namespace rpc = resource::telemetry::v3;

struct Request {
	unsigned value;
};

struct Other {
	unsigned value;
};

std::array<std::byte, 64> request, result, packet;
rpc::RpcClient client{0, request, result, packet};

#ifdef RPC_CLIENT_NEG_COPY
auto copied = client;
#endif
#ifdef RPC_CLIENT_NEG_MOVE
auto moved = std::move(client);
#endif
#ifdef RPC_CLIENT_NEG_UNTYPED
rpc::FieldEndpoint<unsigned> endpoint{42};
#endif
#ifdef RPC_CLIENT_NEG_REQUEST_TYPE
void rejected()
{
	(void)client.beginCommand(rpc::CommandEndpoint<Request>{0, rpc::trustedRpcMapping}, Other{});
}
#endif
#ifdef RPC_CLIENT_NEG_FIELD_VOID
rpc::FieldEndpoint<void> endpoint{0, rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_CONST_TYPE
rpc::FieldEndpoint<const unsigned> endpoint{0, rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_UNSUPPORTED
void rejected()
{
	(void)client.beginReadField(rpc::FieldEndpoint<unsigned*>{0, rpc::trustedRpcMapping});
}
#endif
#ifdef RPC_CLIENT_NEG_DECODE_CONST
void rejected()
{
	const unsigned output = 0;
	(void)client.decodeResult(output);
}
#endif
#ifdef RPC_CLIENT_NEG_COMMAND_SCALAR
rpc::CommandEndpoint<unsigned> endpoint{0, rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_SERVICE_SCALAR_REQUEST
rpc::ServiceEndpoint<unsigned, Request> endpoint{0, rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_SERVICE_SCALAR_RESPONSE
rpc::ServiceEndpoint<Request, float> endpoint{0, rpc::trustedRpcMapping};
#endif
