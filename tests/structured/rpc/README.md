# RpcMailbox qualification

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

The [RPC contract](../../../doc/RpcMailboxV1.md) defines the optional
`resource::telemetry::v3` mailbox. This suite links its explicit provider,
the existing compiled Model adapter and the unchanged Resource protocol.

Run from the repository root:

```sh
python tests/structured/rpc/run.py --cxx g++ --build-dir build/rpc-gcc
python tests/structured/rpc/run.py --cxx clang++-18 --sanitize --build-dir build/rpc-san
python tests/structured/rpc/run.py --cxx g++ --null-checks --build-dir build/rpc-null
python tests/structured/rpc/run.py --arm --cxx /path/to/CubeIDE/tools/bin/arm-none-eabi-g++ --build-dir build/rpc-arm
python tests/structured/rpc/run.py --arm --null-checks --cxx /path/to/CubeIDE/tools/bin/arm-none-eabi-g++ --build-dir build/rpc-arm-null
```

Resolve the installed CubeIDE compiler before a local qualification run; the
runner records its path, version and executable hash. The shared CI ARM job
uses its installed GNU Arm compiler, whose identity is recorded separately.
Neither result establishes MCU execution or a hardware receipt.

Host mode executes the transfer/dispatch fixture with zero-byte and default
32-byte local-object budgets, repeats it with allocation traps, and executes
the [integration example](../../../examples/rpc_mailbox/main.cpp). Standalone
public headers and intended copy/move/temporary-borrow diagnostics are checked
independently. Runtime condition totals come from the executed fixtures and
must agree across the storage/no-heap variants. Each fixture must execute at
least 2,577 checks, the example at least 34, and the host aggregate at least
7,765. A reduction fails with the named `RPC_COVERAGE_FLOOR` diagnostic;
increased counts are allowed. The summary retains measured counts separately
from these minimum expectations. Coverage controls prove that 2,576 fixture
checks, 33 example checks and an aggregate of 7,764 are rejected, while exact
minimum and increased counts pass.
Both modes link real read/write/process references with section garbage
collection and require a mismatched structured-layout mailbox to fail at all
three tagged method boundaries.

ARM mode compiles and links retained read/write/process/reset/size roots under
`-O2`, `-Os` and `-Og`, keeps disassembly, section sizes and `.su` files, and
requires the complete linked roots to contain no allocation or formatting
symbols. A separate linked control retains real newlib allocation/formatting
symbols and must be rejected. Individual provider, protocol and root frames
are limited to 192 bytes. Only `RpcMailbox::processImpl`, which calls the
existing ModelView-by-value dispatch boundary, has a separate 256-byte limit.
The compiled Model adapter has its own 320-byte limit. The actual frame and
its applicable limit are retained for every function. A compiled 4 KiB frame
control must fail the frame gate.
These are individual compiler frames, not a bound on a nested call chain.
The integration example is linked separately for each ARM optimization level.

Each command writes its exact arguments, exit status and output to the selected
build directory. `summary.json` records actual commands, intended rejections,
host executions, executed conditions, compiler identity, controls and ARM
artifacts. It also records expected coverage minima, the coverage controls and
whether host coverage was checked; ARM mode does not apply host count minima.
Before building, the runner records Git HEAD, dirty paths and exact
hashes of all library inputs (including pinned vendor files), RPC fixtures,
the opt-in manifest, example, contract and CI workflow. A successful run requires
the input hashes to remain unchanged through completion. A dirty source tree
is recorded as such and is not represented as a published commit.
ARM mode reports zero executed conditions. No board, probe or port is accessed
by this runner.

The separate [H7S UART integration fixture](h7s/README.md) builds bounded
images offline and can execute real Resource packets on an explicitly
authorized board. Its synthetic verifier controls do not establish MCU
execution; hardware receipts separately record UART checks and complete
Flash restoration. Its external session controls simulate application
disconnects and do not establish Ethernet or physical cable-removal behavior.
