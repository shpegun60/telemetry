# Portable asynchronous client

The C++ headers are in `lib/client`; public includes use
`<client/DeviceClient.hpp>` and related paths.

The queue depends on the standard library and shared callback utilities. It
does not require a local Model, Registry, Workspace or Resource. Generated
`ClientBindings.hpp` supplies the standalone DTO and codec policy.

Backend adapters live in Integration: `<integration/encoded/Backend.hpp>` and
`<integration/rpc/Backend.hpp>`. Encoded uses the shared TEQ1 contract.
The native RPC packet engine remains in `rpc/RpcClient.cpp`; its typed API
also includes the native codec and Resource packet contracts.

See the [client contracts](../../doc/AsyncClient.md),
[web runtime](web/README.md), and [examples](../../examples/async_client/README.md).
