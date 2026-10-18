# Static RPC session qualification

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This suite qualifies the [session pool contract](../../../doc/RpcSessionsV1.md)
with real mailboxes, Resource packets and serialized Model dispatch. Run from
the repository root:

```sh
python tests/structured/rpc_sessions/run.py --cxx g++ --build-dir build/rpc-sessions-gcc
python tests/structured/rpc_sessions/run.py --cxx g++ --null-checks --build-dir build/rpc-sessions-null
python tests/structured/rpc_sessions/run.py --cxx clang++-18 --build-dir build/rpc-sessions-clang
python tests/structured/rpc_sessions/run.py --cxx clang++-18 --sanitize --build-dir build/rpc-sessions-san
python tests/structured/rpc_sessions/run.py --arm --cxx /path/to/CubeIDE/arm-none-eabi-g++ --build-dir build/rpc-sessions-arm
```

The suite runs the same fixture at zero-byte and default 32-byte local-object
policies and repeats it with C++ allocation traps. Each must execute at least 498
conditions and all three measured totals must agree. A separate isolated
exhaustion fixture executes at least 28 checks for generation and reply-ticket
exhaustion. It preloads dependencies before exposing only the pool's private
state in its own translation unit; no production inspection hook is added.
The [multi-session example](../../../examples/rpc_multi_session/main.cpp) is
compiled and executed separately with a minimum of 247 checks. The host aggregate
must reach 1,769 conditions. Counts are parsed from actual execution and
stored independently from coverage minima. Controls reject each reduced floor
and malformed, missing, repeated, zero or failing reports.

The pool header compiles alone. Eight rejected compilations require specific
ownership, capacity, binding or temporary-mailbox diagnostics. Two independent
header consumers and a third main translation unit link and execute together.
The pool is header-only; the existing compiled mailbox layout mismatch gate
remains in the [mailbox suite](../rpc/README.md).

Offline Cortex-M7 qualification compiles and links real retained acquire,
handle, successful-reply handoff, bounded process and release roots under
`-O2`, `-Os` and `-Og`. The production source set contains only the existing
mailbox, protocol, Model adapter and ABI source. The example separately adds
the actual client implementation. Linked symbols must retain all operation
roots and compiled mailbox read/write/process boundaries, while allocation
and formatting symbols are refused. A separate control retains actual newlib
allocation/formatting functions and must be refused. A real compiled 4 KiB
frame must also fail the frame gate. The ordinary frame limit is 192 bytes;
the existing mailbox process boundary has a 256-byte limit and the Model
adapter 320 bytes. These are individual compiler frames, not a total call-chain
bound. Disassembly, section sizes, `.su` reports and image hashes are retained.

The shared [runner](../rpc_extension.py) captures compiler identity, Git HEAD,
dirty paths, and exact hashes of all library inputs, this suite, its example,
contract, shared gates and CI workflow. Completion requires unchanged input
hashes. Each command and its complete result is saved under the selected build
directory, alongside `summary.json`. ARM mode reports zero executed conditions;
it does not access a board, probe or port. Local CubeIDE 14.3 and CI's installed
GNU Arm toolchain are distinct recorded identities. No new hardware receipt
is established by these runs.
