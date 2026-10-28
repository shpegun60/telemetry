# Read-only Qt/QML Universal Inspector

This application displays a Descriptor v3 without a generated SDK. It builds on
the portable [InspectorModel](../../doc/InspectorModel.md), keeps one selected
Field snapshot, and uses the existing DynamicClient / DeviceClient FIFO.

The default **Demo device** is an in-process native Telemetry Model. Refresh
passes actual Encoded request/reply packets through its dispatcher. Demonstration
values are not measurements from a connected controller. Importing a Descriptor
file enables metadata browsing only; its endpoint IDs are never sent to the demo.

## Build and run

Requires Qt 6.4 or newer, including Quick Controls and Network, CMake and a matching C++20
compiler. Use an external build directory:

```sh
cmake -S examples/inspector_qml -B /tmp/telemetry-inspector-qml -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build /tmp/telemetry-inspector-qml -j2
/tmp/telemetry-inspector-qml/telemetry-inspector-qml
```

The CMake project uses installed Telemetry components when found; otherwise it
builds only `inspector`, `encoded_backend`, `tcp_server`, `core` and their
dependencies from this repository. Qt stays in this host application.
An installed package must contain every selected component. Use
`-DCMAKE_DISABLE_FIND_PACKAGE_Telemetry=ON` to explicitly choose this checkout
when an older incomplete installation is present.

On Windows use the Qt kit's matching MinGW compiler before other GCC toolchains
in `PATH`, and pass `CMAKE_CXX_COMPILER`, `CMAKE_MAKE_PROGRAM` and
`CMAKE_PREFIX_PATH` explicitly if multiple kits are installed.

Expand **Fields**, then a catalog. Select a Field and press **Refresh**. Selecting
does not issue a request. Commands and Services are listed for discovery only.
Large aggregates fetch at most 128 immediate children per call; **Load next 128**
adds another batch. Expanding a nested aggregate fetches its own first batch.
There is no recursive expand-all operation. Fetched rows are retained until the
snapshot is retired, so viewing every element can still consume proportional Qt
memory.
This example owns at most 65,536 bytes per Field snapshot. Larger payloads are
refused within that bound; metadata can still be browsed.

To export or inspect a reproducible Descriptor:

```sh
telemetry-inspector-qml --write-demo-descriptor /tmp/demo-descriptor.bin
telemetry-inspector-qml --descriptor /tmp/demo-descriptor.bin
```

The demo exposes `measure.Voltage`, exact `device.Counter`/`device.Offset`, an
unknown signed enum, a 16,384-element array, an empty struct and nested arrays.
## TCP connection and Descriptor discovery

Enter the device address and TCP port in the window, or launch directly:

```sh
telemetry-inspector-qml --tcp 10.50.0.0:5577
```

Use the controller's actual configured address. The application opens one
asynchronous `QTcpSocket`, enumerates Resource files with LIST, finds the exact
path `/telemetry/descriptor.bin`, checks STAT and downloads the complete file
with READ. The file index is discovered, never assumed to be zero. Only a valid
Descriptor is admitted. Discovery has a 10-second exchange timeout and a
60-second overall limit. Missing paths, malformed lengths, unexpected tails,
EOF or timeout close the connection without automatic packet replay.

Subsequent Refresh requests use the existing `EncodedBackend` and Inspector
FIFO. TCP fragments are accumulated into complete existing TEQ1 frames; no
additional wire header, RPC layer or execution queue is introduced. A request
is copied before `prepareHandoff()`, then only its unsent tail is written. Qt
owns each accepted socket-write copy. Replies retain their received session and
request ID; old identities cannot be relabelled as the current request.

The supported server profile is a direct, single-active-client connection with
synchronous dispatch. The server must finish dispatch and release the previous
connection before serving a new client's Descriptor. Both the included host
server and the STM32 integration follow this profile. Concurrent multi-session
servers, proxies that switch remote identities and authentication are outside
this example's contract.

Run a reproducible local TCP device in a second terminal:

```sh
telemetry-inspector-qml --serve-demo 5577
telemetry-inspector-qml --tcp 127.0.0.1:5577
```

The demo listener binds only to loopback. `--serve-demo 0` selects and prints an
ephemeral port. It uses the real native Model, Resource DescriptorFile and
[TCP server adapter](../../lib/integration/tcp/README.md); its values remain
demonstration data. UART/DMA is a later transport port.

## Ownership and shutdown

The controller owns descriptor bytes, runtime indexes, the workspace control
object, backend storage and one portable Inspector. Models copy labels and scalar
text into Qt-owned storage; they retain sealed handles, never borrowed callback
views. U64/S64 and enum codes remain decimal strings all the way into QML.
Floating-point display preserves negative zero and identifies NaN; display text
does not preserve every NaN payload bit.

An accepted Refresh retires the previous snapshot. Rejected submission leaves
the current Inspector snapshot intact. Schema/session invalidation clears old
values. Changing the displayed Field supersedes an old read instead of publishing
it under the new selection.

Qt model reset/insertion signals may run application slots synchronously. Both
models guard their mutation phases; controller reconnect, disconnect and refresh
are refused during those phases. Completion signals run after returning from the
portable client. All calls belong to the Qt owner thread.

On replacement/shutdown the transport drops deferred reply copies and completes
logical close/drain before freeing schema storage. The demo port releases TX
synchronously when dispatch returns. TCP abort releases local socket-owned
storage; it does not prove remote session closure. An uncertain handed-off read
retains its closed, drained Inspector until a fresh Descriptor bootstrap from
the same requested host/port and actual peer IP establishes remote cleanup
under the single-active-client contract. Only then is `confirmQuiescence()`
called before replacing that state. Switching to another peer is refused while
this recovery remains unresolved. Idle completed clients can be destroyed after
local release; another device receives an independent core and binding.

Process shutdown can discard the retained recovery record after local release
and logical drain; it never reports that as remote closure or successful
operation completion. A physical UART/DMA port would need separate evidence
that its driver has stopped reading borrowed buffers.

## Checks

The [Qt test suite](../../tests/inspector_qml/README.md) checks the actual models,
controller and peer. `--smoke` loads the real QML window, performs a FieldRead and
exits successfully only after a published snapshot and materialized endpoint/value
delegates. `--smoke --screenshot FILE.png` also saves the rendered window. Windows
offscreen qualification supplies the OS font directory explicitly; the normal
native window uses platform fonts. Offscreen CI does not qualify every display
server or physical hardware.

The [TCP suite](../../tests/inspector_tcp/README.md) additionally runs real
loopback sockets, fragmented discovery and reads, disconnects, partial writes,
timeouts, stale replies, Descriptor replacement and a separate-process server
with the actual QML window. These host results and offline firmware compilation
do not establish Ethernet/DMA behavior on a connected STM32.
