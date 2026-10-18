# Generated DTOs with the existing RpcClient

[main.cpp](main.cpp) admits the descriptor header, adapts a generated endpoint
with the explicit trusted mapping tag and drives the unchanged asynchronous
RpcClient through a complete-packet Resource loopback into RpcMailbox.
The small packet buffers force fragmentation. `decodeResult` preserves the
existing completion, dispatch, endpoint-status and exact-local-type checks.

Build `codegen_rpc` using the [shared CMake example](../codegen_cpp/README.md).
For real transports preserve generation/request IDs and pending send-buffer
ownership as described in [RpcClientV1](../../doc/RpcClientV1.md).
