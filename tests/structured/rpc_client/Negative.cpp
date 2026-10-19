/*
 * @file Negative.cpp
 * @brief Intended compile failures for client lifetime and exact typed API.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <client/rpc/RpcClient.hpp>
#include <array>
#include <utility>

struct Request {
	unsigned value;
};

struct Other {
	unsigned value;
};

std::array<std::byte, 64> request, result, packet;
::telemetry::client::rpc::RpcClient client{0, request, result, packet};

#ifdef RPC_CLIENT_NEG_COPY
auto copied = client;
#endif
#ifdef RPC_CLIENT_NEG_MOVE
auto moved = std::move(client);
#endif
#ifdef RPC_CLIENT_NEG_UNTYPED
::telemetry::client::rpc::FieldEndpoint<unsigned> endpoint{42};
#endif
#ifdef RPC_CLIENT_NEG_REQUEST_TYPE
void rejected()
{
	(void)client.beginCommand(
	    ::telemetry::client::rpc::CommandEndpoint<Request>{
	        0, ::telemetry::client::rpc::trustedRpcMapping},
	    Other{});
}
#endif
#ifdef RPC_CLIENT_NEG_FIELD_VOID
::telemetry::client::rpc::FieldEndpoint<void> endpoint{0,
                                                       ::telemetry::client::rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_CONST_TYPE
::telemetry::client::rpc::FieldEndpoint<const unsigned> endpoint{
    0, ::telemetry::client::rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_UNSUPPORTED
void rejected()
{
	(void)client.beginReadField(::telemetry::client::rpc::FieldEndpoint<unsigned*>{
	    0, ::telemetry::client::rpc::trustedRpcMapping});
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
::telemetry::client::rpc::CommandEndpoint<unsigned> endpoint{
    0, ::telemetry::client::rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_SERVICE_SCALAR_REQUEST
::telemetry::client::rpc::ServiceEndpoint<unsigned, Request> endpoint{
    0, ::telemetry::client::rpc::trustedRpcMapping};
#endif
#ifdef RPC_CLIENT_NEG_SERVICE_SCALAR_RESPONSE
::telemetry::client::rpc::ServiceEndpoint<Request, float> endpoint{
    0, ::telemetry::client::rpc::trustedRpcMapping};
#endif
