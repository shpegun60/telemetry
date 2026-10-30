# Inspector, shared snapshot and Workbench acceptance

Run from the repository root with a fresh external directory:

```sh
python3 -B tests/inspector/run.py --cxx g++ --build-dir /external/inspector-gcc
python3 -B tests/inspector/run.py --cxx clang++-18 --sanitize --build-dir /external/inspector-san
```

`--arm-cxx arm-none-eabi-g++` additionally compiles transport-free public read,
handoff, child navigation and exact decimal conversion for Cortex-M7 and checks
undefined symbols for allocation.
It does not execute ARM code or test a physical transport.

The same runner also qualifies [FieldWrite Workbench](../workbench/README.md)
with real Encoded/RPC writes, independent readback outcomes and allocation traps.
`Snapshot.cpp` checks the extracted fixed-storage SnapshotStore, including
canonical publish preflight, aliasing, nested prepared scalar paths, zero-byte
siblings, typed navigation and stale identity refusal. Inspector still uses the
same public read-only API and the already-validated publication path; extraction
does not add another recursive scan of every received DTO.

The native fixture publishes a nested 91-byte value, all fixed scalar kinds,
signed and unsigned 64-bit extremes, known and unknown enum values, nested
arrays, empty structures, a zero-length array and 65,536 zero-wire elements.
`Host.cpp` reads it through the actual Encoded dispatcher and a real
`RpcClient`/`RpcSessionPool<1>`/`RpcMailbox` exchange with fragmented results.
Commands and services appear in metadata; their callbacks and the writable
field setter must remain uncalled. The native producer has workspace for its
ordinary C++ return object; this is separate from the Inspector's bounded
canonical byte snapshot.

Both transports also read six F32/F64 pattern pairs through the existing Packet
members: positive and negative quiet NaNs, positive and negative signaling NaNs
with distinct payload bits, and both signs of zero. Assertions use the exact
variant alternative and `std::bit_cast` to compare integer representations;
they do not compare or perform arithmetic on the floating values. Each snapshot
is checked again after response/workspace reuse and peer-value replacement.

The assertions cover exact scalar variants and decimal conversion, owned bytes
after RX/workspace reuse, lazy child bounds, revision invalidation on refresh,
transactional capacity refusal, FIFO capacity, cancellation, timeout, malformed
reply and canonical-value refusal. A valid replacement descriptor preserves
endpoint ID and wire length while changing the root type, so stale admission
cannot be accepted using only size or ID checks.

`Lifetime.cpp` deallocates descriptor and all five metadata arrays after reset
or explicit invalidation while keeping the workspace control object alive. It
also checks old completions versus new selections, reentrant refresh/close,
same-schema reparse, session drift, foreign handles and copied callback results.
These cases run under ASan/UBSan in the sanitizer profile.

`Negative.cpp` has an executable positive control followed by eleven separate
compile refusals for mutation/invocation methods, raw submission, client/core
access and forged node coordinates. `NoHeap.cpp` runs the native acceptance
suite with global C++ allocation traps; a separate deliberate allocation must
reach the trap and exit 86. The trap executable uses the ordinary allocator
configuration even in a sanitizer run.

The runner records commands, exit codes, counted assertions, log hashes and
actual compiler dependency hashes before and after execution. It refuses a
repository/ancestor/in-tree output directory and a nonempty output directory.
It also installs selected modules into an external prefix, then builds and
runs all three CTests of the [standalone example](../../examples/inspector).
The checks establish host protocol and lifetime behavior, not physical device
timing or a desktop GUI.
