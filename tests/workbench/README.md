# Workbench checks

Run the shared Inspector / Workbench runner from the repository root:

```sh
python3 -B tests/inspector/run.py --cxx g++ --arm-cxx arm-none-eabi-g++ --build-dir /external/workbench-gcc
python3 -B tests/inspector/run.py --cxx clang++-18 --sanitize --build-dir /external/workbench-san
```

The runner also checks the unchanged read-only Inspector API and shared
SnapshotStore. Workbench executes real Encoded and fragmented RPC exchanges,
editing structs, arrays, exact U64/S64, open enums and scalar leaves while
preserving unedited F32/F64 qNaN, sNaN and signed-zero bits. Device-side
normalization is observed only through a new readback.

Tests cover read-only permissions, wrong types, stale nodes/schemas, invalid
timeout budgets, QueueFull draft retention, callback reentrancy and close/drain.
Write and Refresh results are checked independently, including confirmed Write
followed by refused or timed-out Refresh. Caller clocks and explicit
queued/offered/prepared states establish timeout and OutcomeUnknown boundaries;
late replies cannot complete a request twice or trigger a write replay.

NoHeap runs the same native workflow with global C++ allocation traps and a
deliberate allocation control exiting 86. Separate compile refusals prove that
the facade exposes no raw-payload Command/Service submission or client/core access,
and rejects a non-callable completion. The standalone dependency probe excludes
Model/reflection, Resource and concrete wire/backend dependencies. Optional ARM
compilation records symbols and individual compiler frames; it does not run on
a board or measure complete call-chain stack.

The existing [Qt/QML runner](../inspector_qml/README.md) additionally operates the
actual Edit, TextField and Apply QML handlers. The [TCP runner](../inspector_tcp/README.md)
checks opt-in, partial writes, normalizing setters, refusal, lost Write ACK,
reconnect without replay and Write ACK followed immediately by EOF.

Commands, source/binary hashes, assertion counts and logs are retained outside
the checkout. Hardware timing and writable UART/DMA are outside this stage.

`Command.cpp` exercises the same portable facade through Encoded and RPC:
canonical initial arguments, Void versus empty Struct, exact scalar/enum/array
editing, unchanged argument submission, Accepted versus Executed, queue refusal,
stale identities and execution-boundary outcomes. A separate allocation-trap
executable runs those Command cases and a deliberate allocation control. The
existing FieldWrite and read-only Inspector suites remain mandatory regressions.

`Service.cpp` uses the same Encoded/RPC fixture with caller-owned response storage.
It distinguishes Void from empty Struct and Service InvalidArgument from Command
Accepted, verifies the response copy after RX reuse, and keeps operation/value
outcomes independent. Capacity, overlap, stale identities, queued/offered/prepared
termination and callback reentrancy are checked before exposing response handles.
The allocation-trap wrapper executes the same suite and an exit-86 control.
