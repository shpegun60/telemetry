# Qt Quick generated API example

This runnable example uses the SDK generated from `tests/codegen/Device.hpp`.
The same QML UI can use real native Encoded dispatch (default) or the existing
`RpcClient`, `RpcMailbox`, and Resource protocol (`--rpc`). Both modes operate on
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

The direct handler checks `requestActive(token, session)` immediately before
calling the real `readFieldEncoded`, `writeFieldEncoded`,
`executeCommandEncoded`, or `callServiceEncoded` function. It uses disjoint
payload/output/workspace storage and forwards actual dispatch, family endpoint
status, and written bytes. These fixture callbacks are small synchronous work;
an application with slow callbacks should place its transport work on a worker
and define its cancellation/handoff policy.

The RPC mode drives the optional generated `QmlRpcAdapter.hpp` and the existing
caller-buffered `RpcClient` from a 1 ms event-loop timer. It exchanges complete
Resource frames with the actual mailbox, including request fragmentation through
the 48-byte packet buffer, and pairs replies by session generation/request ID.
The borrowed client and all packet/workspace buffers outlive the adapter. Packet
handoff and the loopback reply occur in the same callback, without yielding to
another UI event. There is no blocking I/O or nested event loop. For a physical
transport, the application supplies serial/socket framing and the packet
handoff/reply callbacks; session loss never automatically replays an operation.

This example binary links both native dispatch and RPC implementation files.
The generated main QML module has no mandatory RPC dependency. See
[GeneratedQmlApi.md](../../doc/GeneratedQmlApi.md) for the transport and lifetime
contract.
