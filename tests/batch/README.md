# Optional Encoded Mixed Batch qualification

The maintained runner builds only the selected public components, installs them,
moves the install prefix, then builds the consumer against that relocated SDK.
Generated files and logs must be outside the checkout. Nothing accesses a board.

```sh
python3 -B tests/batch/run.py --build-dir /tmp/telemetry-batch-gcc --cxx g++
python3 -B tests/batch/run.py --build-dir /tmp/telemetry-batch-sanitized --cxx clang++-18 --sanitize
python3 -B tests/batch/run.py --build-dir /tmp/telemetry-batch-arm --cxx arm-none-eabi-g++ --arm
python3 -B tests/batch/qmake.py --build-dir /tmp/telemetry-batch-qmake --qmake qmake6
```

Host execution covers canonical capability/request/reply bodies, whole-request
preflight before endpoint calls, ordered mixed operation outcomes, the existing
FIFO, admission/revocation, retry boundaries and response lifetime. The same
client lifecycle test runs with heap allocation traps armed. Six deliberate
allocation attempts must terminate with code `86`; separate negative compile
controls prove private binding state and insufficient callback storage are
rejected.

`CrossGroup.cpp` drives a real Model, Descriptor, RuntimeSchema, DynamicClient
and Encoded dispatcher through twelve ordered operations over catalog groups
1, 2 and 3 with nonconsecutive local indices. It checks complete wire IDs,
independent Field/Command/Service address spaces, repeated Write/Read IDs,
per-item results and an independent execution trace. Unselected endpoints,
including all group-zero decoys, must remain untouched. This regression runs
through the relocated installed SDK on both host profiles; it is not a board
test or an exactly-once delivery guarantee.

ARM mode compiles the full runtime bind/submit/completion path as an object and
links selected legacy and optional server roots at `-Os` and `-O2`. It retains
section sizes, symbols, disassembly and compiler `.su` records. Allocation
symbols fail qualification, as does the optional executor appearing in the
legacy root. These are offline compile/link measurements, not whole-firmware
Flash deltas, total peak stack or physical MCU results.

`summary.json` records exact command lines, compiler identities, hashes of logs,
installed files, source inputs and generated artifacts. A successful run checks
that neither the installed SDK nor source inputs changed during execution.

The qmake runner separately checks seven source inventories, including late
opt-in, repeated includes, standalone server inclusion and combined ReadMany /
Mixed Batch selection. It does not compile those qmake configurations. The
independent module runner in `tests/modules` also checks that `batch_client`
compiles without Telemetry Core and that `batch_server` has no Client/Resource
dependency.
