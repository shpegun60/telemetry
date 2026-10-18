# Typed RpcClient qualification

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This suite qualifies the [typed client contract](../../../doc/RpcClientV1.md).
Run from the repository root:

```sh
python tests/structured/rpc_client/run.py --cxx g++ --build-dir build/rpc-client-gcc
python tests/structured/rpc_client/run.py --cxx g++ --null-checks --build-dir build/rpc-client-null
python tests/structured/rpc_client/run.py --cxx clang++-18 --build-dir build/rpc-client-clang
python tests/structured/rpc_client/run.py --cxx clang++-18 --sanitize --build-dir build/rpc-client-san
python tests/structured/rpc_client/run.py --arm --cxx /path/to/CubeIDE/arm-none-eabi-g++ --build-dir build/rpc-client-arm
```

Resolve the installed compiler rather than reusing a recorded path. The runner
captures its executable hash/version, Git HEAD, dirty paths and exact hashes of
all library files, this suite, its example, contract, shared gates and CI
workflow. A successful run requires these inputs to remain unchanged. The
summary describes the actual dirty input set; it is not a published-SHA claim.

Host execution runs the real client/mailbox loopback fixture with zero-byte
and default 32-byte local-object policies, repeats the default fixture with
C++ allocation traps, and executes the [example](../../../examples/rpc_client/main.cpp).
Counts come from the executed conditions. Each fixture currently has a minimum
of 1,730 conditions, the example 45, and the aggregate 5,251. A separate isolated
fixture executes at least 16 request-ID, transaction-ID and generation-boundary
checks. It includes the actual implementation in its sole translation unit;
the runner links no second client definition into this control. The runner retains
actual counts separately from floors and requires the three fixture totals to
agree. Controls reject missing, repeated, zero or failing runtime reports;
every coverage floor rejects its count minus one and accepts both its exact
minimum and an increased count.

The public client and wire headers compile independently. Eleven compile
rejections require their intended type/lifetime diagnostic. Two independently
compiled API consumers plus a third main translation unit must link and execute
together, including agreement of the same local type token across translation
units. The compiled client has no dependency on structured object-layout
configuration, so no new compiled layout-mismatch gate is claimed. The existing
server ABI gate remains in the [mailbox suite](../rpc/README.md).

Offline ARM qualification compiles and links real retained begin, packet,
accepted-send, reply, process, large-result decode and reset roots under `-O2`,
`-Os` and `-Og`. Its production image compiles only `RpcClient.cpp`; the
loopback fixture and example use separately listed server sources. Each root
must remain in the linked image, and required compiled client methods must be
present. Full linked-symbol inspection rejects allocation and formatting
dependencies. A separate image retaining real newlib allocation/formatting
functions must fail that gate. A real compiled 4 KiB frame must fail the frame
gate. Stack reports describe individual compiler frames, not nested call chains.
The suite records disassembly, section sizes, `.su` files and image hashes.
The integration example is also linked separately at each optimization level.

The shared [runner](../rpc_extension.py) records every command, result and exact
count in the selected build directory. ARM mode reports zero runtime conditions
and does not access a board, probe or port. Local CubeIDE 14.3 and CI's installed
GNU Arm toolchain are distinct recorded compiler identities. Neither offline
result establishes MCU execution.
