# Portable asynchronous client

Include a generated `ClientBindings.hpp` and construct its `client::DeviceClient`
with an [EncodedBackend](encoded/EncodedBackend.hpp) or
[RpcBackend](rpc/RpcBackend.hpp), stable buffers and a monotonic clock.
[Complete contracts and examples](../../../doc/AsyncClient.md).

The C++ core has no Qt, HAL, RTOS, thread, mutex or mandatory heap dependency.
One owner services it through `process(nowMs)`. `ThreadSafeClient` is optional;
instantiate its DeviceClient with an application-supplied mutex policy. The
wrapper never invokes callbacks or performs transport work in producer threads.

qmake: include `client.pri`; select `CONFIG += telemetry_client_rpc` for RPC.
CMake: `add_subdirectory(lib/telemetry/client)` then link `telemetry_client`;
enable `TELEMETRY_CLIENT_RPC` and link `telemetry_rpc_client` for RPC.
The Resource server manifest does not select client sources. To preserve old
RPC include paths explicitly add the separate `compat` include root
(`CONFIG += telemetry_client_compat` with qmake). The native class keeps its
existing canonical namespace and symbols; the public client namespace exposes
aliases. This is source/build migration, with unchanged Descriptor/RPC bytes.

The [web runtime](web/README.md) has one TypeScript source and emitted JS/types.
The generated Qt adapter delegates execution to this C++ queue and keeps only
QML token bookkeeping. Native Model/Encoded APIs remain independent modules.
