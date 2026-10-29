# Read-only Qt/QML Universal Inspector

This application displays a Descriptor v3 without a generated SDK. It builds on
the portable [InspectorModel](../../doc/InspectorModel.md), keeps one selected
Field snapshot, and uses the existing DynamicClient / DeviceClient FIFO.

The default **Demo device** is an in-process native Telemetry Model. Refresh
passes actual Encoded request/reply packets through its dispatcher. Demonstration
values are not measurements from a connected controller. Importing a Descriptor
file enables metadata browsing only; its endpoint IDs are never sent to the demo.

## Build and run

Requires Qt 6.4 or newer with Core, Gui, Qml, Quick, Quick Controls 2 and Network,
CMake, Ninja and a matching C++20 compiler. Qt SerialPort and COBS are optional
for UART; the default build provides the local demo, Descriptor files and TCP.
In Qt Creator, open this directory's `CMakeLists.txt` and choose the matching kit.
The root `telemetry.pro` is a separate Qt Widgets playground, not this application.

From the Telemetry root, use a fresh external build directory:

```sh
cmake -S examples/inspector_qml -B /tmp/telemetry-inspector-qml -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build /tmp/telemetry-inspector-qml -j2
inspector_exe=/tmp/telemetry-inspector-qml/telemetry-inspector-qml
"$inspector_exe"
```

The CMake project uses installed Telemetry components when found; otherwise it
builds only `inspector`, `encoded_backend`, `encoded_server`, `tcp_server`, `core`
and their dependencies from this repository. Qt stays in this host application.
An installed package must contain every selected component. Use
`-DCMAKE_DISABLE_FIND_PACKAGE_Telemetry=ON` to explicitly choose this checkout
when an older incomplete installation is present.

On Windows, the following PowerShell example uses the qualified Qt 6.10.1 / MinGW
13.1 kit. Replace the paths with one matching installed kit; keep its compiler
ahead of other GCC toolchains. The executable is in the build directory root:

```powershell
$QtPrefix = "C:\Qt\6.10.1\mingw_64"
$QtTools = "C:\Qt\Tools"
$InspectorBuild = "C:\external-build\telemetry-inspector"
$CMake = "$QtTools\CMake_64\bin\cmake.exe"
$env:PATH = "$QtTools\mingw1310_64\bin;$QtPrefix\bin;$env:PATH"
& $CMake -S examples/inspector_qml -B $InspectorBuild -G Ninja `
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtPrefix" `
  "-DCMAKE_CXX_COMPILER=$QtTools/mingw1310_64/bin/g++.exe" `
  "-DCMAKE_MAKE_PROGRAM=$QtTools/Ninja/ninja.exe"
& $CMake --build $InspectorBuild --parallel 2
$InspectorExe = "$InspectorBuild\telemetry-inspector-qml.exe"
& $InspectorExe
```

These commands use the Qt runtime through this shell's `PATH`; they do not deploy
a standalone application or modify the Qt installation. Later shell examples use
`"$inspector_exe"`; in PowerShell use `& $InspectorExe` with the same arguments.
Set the executable variable again when opening another terminal.

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
"$inspector_exe" --write-demo-descriptor /tmp/demo-descriptor.bin
"$inspector_exe" --descriptor /tmp/demo-descriptor.bin
```

The demo exposes `measure.Voltage`, exact `device.Counter`/`device.Offset`, an
unknown signed enum, a 16,384-element array, an empty struct and nested arrays.
## TCP connection and Descriptor discovery

Enter the device address and TCP port in the window, or launch directly:

```sh
"$inspector_exe" --tcp 10.50.0.0:5577
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
"$inspector_exe" --serve-demo 5577
# In another terminal, with inspector_exe set to the same executable:
"$inspector_exe" --tcp 127.0.0.1:5577
```

The demo listener binds only to loopback. `--serve-demo 0` selects and prints an
ephemeral port. It uses the real native Model, Resource DescriptorFile and
[TCP server adapter](../../lib/integration/tcp/README.md); its values remain
demonstration data. The optional serial profile is described below.

## NUCLEO-H7S3L8 through ST-LINK serial

The optional serial build uses Qt SerialPort and the existing external COBS
library. Select the SerialPort module matching the Qt/compiler kit, and the
qualified COBS checkout `2e0abf260848fcb74e4a57b37532188047759643`:

```sh
cmake -S examples/inspector_qml -B /path/to/external/inspector-serial -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt -DINSPECTOR_ENABLE_SERIAL=ON \
  -DCOBS_ROOT=/path/to/cobs -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/external/inspector-serial
inspector_exe=/path/to/external/inspector-serial/telemetry-inspector-qml
"$inspector_exe" --serial /dev/ttyACM0 --baud 115200
```

On Windows, reuse the Qt/tool variables from the PowerShell build above and select
a fresh serial build directory. This form assumes the matching Qt kit includes
SerialPort:

```powershell
$SerialBuild = "C:\external-build\telemetry-inspector-serial"
$CobsRoot = "C:\path\to\COBS"
& $CMake -S examples/inspector_qml -B $SerialBuild -G Ninja `
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtPrefix" `
  "-DCMAKE_CXX_COMPILER=$QtTools/mingw1310_64/bin/g++.exe" `
  "-DCMAKE_MAKE_PROGRAM=$QtTools/Ninja/ninja.exe" `
  -DINSPECTOR_ENABLE_SERIAL=ON "-DCOBS_ROOT=$CobsRoot"
& $CMake --build $SerialBuild --parallel 2
$InspectorExe = "$SerialBuild\telemetry-inspector-qml.exe"
& $InspectorExe --serial COM6 --baud 115200
```

If Qt SerialPort was built into a separate external prefix against that exact kit,
add `-DQt6SerialPort_DIR=C:/external-tools/qtserialport/lib/cmake/Qt6SerialPort`
to the configure command and prepend that prefix's `bin` directory to this shell's
`PATH` before launching. Keep the module external; no copying into `C:\Qt` is
required. This project consumes the selected COBS checkout in place and does not
install dependencies automatically.

`COM6` and `/dev/ttyACM0` are examples; select the actual ST-LINK port in the window.
UART controls appear only in a build configured with `INSPECTOR_ENABLE_SERIAL=ON`.
The matching [Nucleo firmware](../nucleo_h7s3l8/README.md) serves demonstration
values through USART3 DMA. Both peers use
[SerialProfile.hpp](../inspector_serial/SerialProfile.hpp): existing COBS framing/CRC
with at most 2048 body bytes. Each body is an ordinary Resource packet or complete
TEQ1 message, without an extra application route. Descriptor and TEQ1 formats
are unchanged. Field payloads above 2016 bytes remain browsable as metadata but
Refresh refuses them explicitly before changing the snapshot or FIFO.

Only complete, CRC-checked packets reach the Resource/Encoded readers. A short
serial write closes the failed exchange; the library does not replay a prefix
or retry the request. Per-exchange and total discovery deadlines are separate.
The exclusive host sends one request at a time and never runs another serial
producer on the same port.

Recovery requires the supported synchronous, read-only board profile: dispatch
waits for the previous UART transmission and COBS lease to finish, then processes
RX in order. A complete, validated new LIST/STAT/Descriptor transcript from the
same fixed peer is the sequencing barrier. COM close/clear and an isolated LIST
reply do not establish it. USB identity is captured when the port opens;
replacing the MCU or its wiring while recovery is pending is outside this
profile. Malformed or stale bootstrap packets close the attempt without new
admission. This does not qualify a general asynchronous serial server or RPC.

The read-only headless check also uses the real Controller/Qt models:

```sh
"$inspector_exe" --serial /dev/ttyACM0 --baud 115200 --check-device \
  --report /path/to/external/device-report.json
```

With the serial Windows build, use:

```powershell
& $InspectorExe --serial COM6 --baud 115200 --check-device `
  --report C:\external-build\device-report.json
```

It reads all demo Fields, preserves integer values as strings, verifies all
three F32 voltages, the exact U64 Counter, demonstration flag, nested matrix,
boolean and unknown signed enum. Run it on the Nucleo demo firmware.
The [guarded hardware runner](../../tests/nucleo_h7s3l8/README.md)
backs up and restores the original internal Flash. Host PTY tests are maintained
in [inspector_serial](../../tests/inspector_serial/README.md).
The [published Nucleo evidence](../../doc/evidence/inspector-serial-h7s-2026-10-28/README.md)
includes a physical four-Field serial check and the QML window. It qualifies the
captured board/profile and source inputs; support for another board or transport
still needs its own checks.

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
