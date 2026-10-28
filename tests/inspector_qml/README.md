# Qt/QML runtime Inspector checks

This suite builds the [runtime Inspector example](../../examples/inspector_qml/README.md)
against Qt 6.4 or newer. It uses the actual `EndpointModel`, `ValueModel`,
`InspectorController` and native Encoded demo peer, without generated SDK files.

```sh
python3 -B tests/inspector_qml/run.py --build-dir /external/inspector-qml \
  --qt-prefix /usr --cxx /usr/bin/g++ --cmake /usr/bin/cmake --ninja /usr/bin/ninja
```

On Windows select the Qt kit's matching MinGW compiler, Qt prefix, native CMake
and Ninja explicitly. The runner places the selected compiler and Qt runtime
before other toolchains in `PATH`. Every build, log and `summary.json` stays in
a fresh directory outside the repository. Windows offscreen runs explicitly
use the system `Windows/Fonts` directory so the PNG contains readable text.
A missing example or failed/expired command fails the run. Source inventories
and raw SHA-256 digests must remain
unchanged from start to finish.

`QAbstractItemModelTester` checks both models, with explicit lazy-fetch tests
for the 16,384-element array. `QSignalSpy` observes resets, insertions and read
completion. The maintained floor is 480 executed checks and 10 real FieldReads.
Real Encoded reads check exact U64/S64 decimal strings, an unknown
signed enum, nested structs/arrays, an empty struct, owned snapshot storage and
stale model indexes. Controller checks cover metadata-only files, selection
without I/O, Command/Service refusal, accepted refresh invalidation, QueueFull
preservation, delayed old replies, fresh sessions, timeout/recovery and shutdown.
Direct slots on reset and row-insertion notifications attempt reconnect and
disconnect; both must preserve the active model mutation and current session.
Reactive loaded-count notification is checked under the same guard.
All endpoint metadata roles reject schema or admission invalidation immediately,
before any controller pump or Qt model reset; tree topology remains unchanged
until its explicit reset.

The second executable creates the actual QML window offscreen and performs a
FieldRead. Its success marker requires both a root window and a published
snapshot, visible endpoint delegates and four visible value rows including the
decoded `Ua` child. The runner retains those view counts in its summary.
These are host Qt and protocol checks; they do not establish device,
display-server or physical transport behavior.

The runner also retains a PNG of the offscreen window and its dimensions/hash.
On Linux, `--sanitize --cxx clang++-18` enables fatal AddressSanitizer and
UndefinedBehaviorSanitizer checks. Leak detection is explicitly disabled for
the Qt offscreen plugin's process-global allocations; this profile does not
claim LeakSanitizer coverage.
