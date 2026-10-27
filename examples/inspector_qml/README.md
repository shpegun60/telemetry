# Read-only Qt/QML Universal Inspector

This application displays a Descriptor v3 without a generated SDK. It builds on
the portable [InspectorModel](../../doc/InspectorModel.md), keeps one selected
Field snapshot, and uses the existing DynamicClient / DeviceClient FIFO.

The default **Demo device** is an in-process native Telemetry Model. Refresh
passes actual Encoded request/reply packets through its dispatcher. Demonstration
values are not measurements from a connected controller. Importing a Descriptor
file enables metadata browsing only; its endpoint IDs are never sent to the demo.

## Build and run

Requires Qt 6.4 or newer, including Quick Controls, CMake and a matching C++20
compiler. Use an external build directory:

```sh
cmake -S examples/inspector_qml -B /tmp/telemetry-inspector-qml -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build /tmp/telemetry-inspector-qml -j2
/tmp/telemetry-inspector-qml/telemetry-inspector-qml
```

The CMake project uses installed Telemetry components when found; otherwise it
builds only `inspector`, `encoded_backend`, `encoded_server`, `core` and their
dependencies from this repository. Qt stays in this host application.

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

To export or inspect a reproducible Descriptor:

```sh
telemetry-inspector-qml --write-demo-descriptor /tmp/demo-descriptor.bin
telemetry-inspector-qml --descriptor /tmp/demo-descriptor.bin
```

The demo exposes `measure.Voltage`, exact `device.Counter`/`device.Offset`, an
unknown signed enum, a 16,384-element array, an empty struct and nested arrays.
There is no UART/TCP connection implementation in this first GUI example.

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

On replacement/shutdown the local transport drops deferred reply copies, completes
close/drain, and destroys the Inspector before freeing its schema. The demo port
releases its TX copy synchronously when dispatch returns. A future physical port
must additionally prove actual TX/DMA release and remote session cleanup before
reusing a binding; logical drain alone does not establish those facts.

## Checks

The [Qt test suite](../../tests/inspector_qml/README.md) checks the actual models,
controller and peer. `--smoke` loads the real QML window, performs a FieldRead and
exits successfully only after a published snapshot and materialized endpoint/value
delegates. `--smoke --screenshot FILE.png` also saves the rendered window. Windows
offscreen qualification supplies the OS font directory explicitly; the normal
native window uses platform fonts. Offscreen CI does not qualify every display
server or physical hardware.
