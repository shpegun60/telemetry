# Qt Quick generated API example

This runnable example uses the SDK generated from `tests/codegen/Device.hpp`.
The same QML UI can use real native Encoded dispatch (default) or the existing
`RpcClient`, `RpcMailbox`, `RpcSessionPool<1>`, and Resource protocol (`--rpc`). Both modes operate on
the native fixture model in this process; no physical device is connected.

Run the [qualification runner](../../tests/codegen_qml/README.md) first. Its
external output directory contains `sdk` and the native descriptor/payload
fixtures. The example checks the full fixture descriptor against its compiled
model and decodes `config.bin` into the native configuration before admission.
The generated API itself admits only the exact 64-byte schema header.

```powershell
$env:PATH = 'C:/Qt/Tools/mingw1310_64/bin;C:/Qt/6.10.1/mingw_64/bin;' + $env:PATH
$sdkRun = 'C:/temp/telemetry-qml-checks'
cmake -S examples/codegen_qml -B C:/temp/telemetry-qml-example -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  "-DGENERATED_SDK=$sdkRun/sdk"
cmake --build C:/temp/telemetry-qml-example
C:/temp/telemetry-qml-example/telemetry_qml_example.exe `
  "$sdkRun/native/descriptor.bin" "$sdkRun/native/config.bin"
C:/temp/telemetry-qml-example/telemetry_qml_example.exe `
  "$sdkRun/native/descriptor.bin" "$sdkRun/native/config.bin" --rpc
```

For a Linux Qt installation, use the SDK produced by a Linux qualification run:

```sh
cmake -S examples/codegen_qml -B /tmp/telemetry-qml-example -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGENERATED_SDK=/tmp/telemetry-qml-checks/sdk
cmake --build /tmp/telemetry-qml-example
/tmp/telemetry-qml-example/telemetry_qml_example \
  /tmp/telemetry-qml-checks/native/descriptor.bin \
  /tmp/telemetry-qml-checks/native/config.bin --rpc
```

Qt 6.4+ with QuickControls2 and its Qt Quick Templates runtime module is required.
Use the compiler shipped with the selected Windows Qt kit. Add `--smoke` in
either mode to exit after the first successful read, or use the window
interactively. Offscreen automation may set
`QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`.

The UI exercises Field read/write, Command execution, and a Service response.
Inspect returns a checksum over the configuration samples. Separate buttons
show Command `Accepted` and a Service refusal; acceptance is not physical work
completion.

QML creates `DeviceApi`; the C++ host explicitly attaches its input engine,
connects queued `requestReady` handling, and admits the exact generated header
before refreshing. Header admission declares schema identity; it is neither
authentication nor verification of a descriptor body. Each request carries its
session generation and must complete or fail using the same token/generation.
Property reads return cache snapshots and perform no I/O. A successful write
invalidates the cache until a later refresh. `accepted` and completed success
remain separate outcomes; no operation is automatically replayed on reconnect.

I64/U64, including enums with a 64-bit underlying type, cross the QML boundary as
canonical decimal strings. Keep those strings intact instead of converting them
through JavaScript `Number`. The generated QML module uses the C++ codecs rather
than JavaScript BigInt or a runtime descriptor parser. QML maps/lists are
snapshotted and checked before dispatch; conversion failures finish with
`Validation` without transport I/O.

Both modes use the optional generated `DeviceClientAdapter.hpp` and the SAME
portable `DeviceClient` execution FIFO (8 requests and a shared 4096-byte payload
pool by default). The adapter's token map only correlates QML completions; it is
not another execution queue. Full queues explicitly finish with `QueueFull`.
The older single-active `QmlRpcAdapter.hpp` remains an optional compatibility
adapter; this example uses the new common queue.

Default mode exchanges Encoded request/reply v1 packets through `EncodedBackend`
and the actual native `EncodedDispatcher`. RPC mode drives the existing
caller-buffered `RpcClient` through `RpcBackend`, one mailbox and
`RpcSessionPool<1>`, including fragmentation through the 48-byte packet buffer.
Both modes preserve routing, endpoint and codec outcomes. An Encoded response
confirms the reported outcome; this simple protocol supplies no server dedup
ledger or automatic retries. Existing RPC retries retain the same transaction.

A 1 ms event-loop timer drives the owner. The port first copies each complete
packet into bounded port-owned storage, calls `prepareHandoff(session,
requestId)` before sending, and reports `onSendAccepted` only after complete
handoff. Partial writes, DMA buffer release and an endpoint response are
separate events on a physical transport. All borrowed backend buffers outlive
the adapter. Native fixture callbacks are small synchronous work; slow callbacks
belong behind an application worker boundary with its own admission policy.

A successful final RPC WRITE reply receives the exact pool `replyAccepted`
ticket before dispatch. If recovery is required, the application drains cancelled
FIFO completions, excludes old packet copies, releases the actual old pool
handle, acquires a fresh connection, confirms closure and obtains/adopts a matching
header again. Local generation changes do not clear the server mailbox. No old
operation is resubmitted. This fixture cannot remain Running between loopback
ticks; a physical server must also drain a Closing callback before reuse.

Shutdown first stops the timer/producer, calls `close()` and continues owner
processing until `drained()`. Adapter destruction requires that drain and no
executing callback. Physical ports must also stop or drain their DMA/socket
copies. There is no blocking I/O or nested event loop.

This example binary links both native dispatch and RPC implementation files.
The generated main QML module has no mandatory RPC dependency. See
[GeneratedQmlApi.md](../../doc/GeneratedQmlApi.md) for the transport and lifetime
contract.
