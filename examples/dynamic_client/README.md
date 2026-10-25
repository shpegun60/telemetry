# Dynamic host client

[Host.cpp](Host.cpp) parses a real Model Descriptor, resolves a Field and a
prepared member handle, then performs **Read → Edit → Write → Refresh** through
DynamicClient and the actual Encoded dispatcher. The client uses wire bytes;
only the peer defines the native `Config` structure. No generated SDK is needed.

The initial callback copies its borrowed response before returning. Writer edits
that copy in caller-owned staging, and the final read shows the value actually
returned by the peer. The write and refresh share the existing Client FIFO.

Build after installing the selected modules:

```sh
cmake -S . -B /tmp/telemetry-dynamic-install-build \
  '-DTELEMETRY_COMPONENTS=dynamic_client;encoded_backend;encoded_server;core' \
  -DCMAKE_INSTALL_PREFIX=/tmp/telemetry-dynamic-install
cmake --build /tmp/telemetry-dynamic-install-build
cmake --install /tmp/telemetry-dynamic-install-build
cmake -S examples/dynamic_client -B /tmp/telemetry-dynamic-example \
  -DCMAKE_PREFIX_PATH=/tmp/telemetry-dynamic-install
cmake --build /tmp/telemetry-dynamic-example
/tmp/telemetry-dynamic-example/dynamic-host
```

This is a synchronous host transport example. An asynchronous UART/DMA port must
retain its own TX copy until physical release, exclude old frames and close the
old peer session before confirming quiescence. See the
[client contract](../../doc/DynamicClient.md) for invalidation and recovery.
