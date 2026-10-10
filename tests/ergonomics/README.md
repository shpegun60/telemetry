# Additive API qualification

[Test index](../README.md) · [User guide](../../doc/user/Ergonomics.md)

[Recorded host/ARM qualification](RESULTS.md) · [Captured results](results.json)

Run from the repository root; generated files belong to an explicit output:

```sh
python tests/ergonomics/run.py --cxx g++ --build-dir /tmp/telemetry-ergonomics
python tests/ergonomics/run.py --cxx clang++-18 --sanitize --build-dir /tmp/telemetry-ergonomics-asan
python tests/ergonomics/run.py --cxx g++ --null-checks --build-dir /tmp/telemetry-ergonomics-nullchecks
python tests/ergonomics/run.py --arm --cxx arm-none-eabi-g++ --build-dir /tmp/telemetry-ergonomics-arm
```

The existing [cross-endian runner](../structured/codec/endian.py) also builds
ResourceClient.cpp with Protocol.cpp for native little-endian Linux and
big-endian s390x, then executes the latter through QEMU. Both builds force
the expected `std::endian::native` with an injected assertion, use the same
request/reply byte goldens, and retain executable hashes and output in
`summary.json`. Its input manifest includes ResourceClient.cpp and the runner
itself. This runs in the endian CI job and does not access hardware:

```sh
python tests/structured/codec/endian.py --build-dir /tmp/telemetry-endian
```

The suite covers exact native runtime Command/Service selection separately
from application outcomes, factory result lifetime, borrowed Field identity,
owning read refusal reasons, getter-once, stoppable typed/erased iteration,
constexpr maxima and checked resource packet builders/parsers. Negative cases
require the intended diagnostic, including late typed branches after an early
false. Counts printed by programs are computed from executed assertions.

NativeScale runs every row in 128/256-target fixtures, in groups of 32. This
matches the large-project guide and avoids the known giant-tuple PE symbol
table limit; arbitrary monolithic table scalability is not established.
ARM O2/Os profiles retain
sections and individual stack frames for named visitors and convenience APIs;
LargeNativeCodegen O2/Os/Og rejects any hidden response-sized frame (>256 B).
The placement helpers require caller storage large/aligned enough for their
specific result and caller destruction afterward; these are offline probes.

Resource READ builders compare against independent checked explicit encoding.
Their code sections and frame maps must match. The current generic server is
also compared to the retained e263aa4 Protocol.cpp at O2/Os; both compile
against current shared wire constants. Existing wire/descriptor/endpoint gates
remain separate and run in CI. No hardware access occurs in this runner.

summary.json records compiler identity, source head, flags, captured inputs,
execution output, diagnostics and ARM sections/frames. These are host and
offline compiler observations, not device cycle or full call-chain measurements.

## One-level Command and Service outcomes

[Flat API guide](../../doc/user/FlatNative.md) ·
[Runnable example](../../examples/user_guide/FlatNative.cpp) ·
[Measured comparison](FLAT_RESULTS.md) · [Captured results](flat-results.json)

The additive flat facade has a separate runner. The existing runner above keeps
its detailed results, traversal and resource-client gates. Run both:

```sh
python tests/ergonomics/flat.py --cxx g++ --build-dir /tmp/telemetry-flat
python tests/ergonomics/flat.py --cxx g++ --null-checks --build-dir /tmp/telemetry-flat-null
python tests/ergonomics/flat.py --cxx clang++-18 --sanitize --build-dir /tmp/telemetry-flat-asan
python tests/ergonomics/flat.py --arm --cxx arm-none-eabi-g++ --build-dir /tmp/telemetry-flat-arm
```

FlatCalls checks all routing/application statuses, local/global and empty
tables/catalogs, exact request/response and ownership selection before callbacks,
void results, wide/signed/local-enum IDs, empty function/owner slots, borrowed
address identity, final-storage construction and exceptions. It executes 671
checks with host exceptions enabled; four exception checks are omitted in
no-exceptions builds. Heap use aborts the host fixture. Deleted copy/move
payloads and `-fno-elide-constructors` retain the final-storage control.

FlatNegative requires 57 intended diagnostics, including cv/ref/pointer payload
types, volatile requests, implicit/explicit ID narrowing, rvalue table calls and
incorrect result factories. FlatScale executes every row in grouped 128/256
profiles (1,934/3,854 checks). FlatLargeCodegen executes 79 checks for each exact
1 KiB and 4 KiB response size and checks borrowed pointer identity separately.

ARM O2/Os/Og builds retain sections, disassembly and individual stack frames for
manual visitor, existing detailed API and new flat API entrypoints. Large
optimized old/flat entrypoint code bytes must match. The runner rejects large
frames above 256 B and allocation/hidden-copy references in measured objects.
Entrypoint instruction bytes and ELF relocation targets are retained separately.
Only objects built by the current invocation are inspected; an incomplete
summary replaces any older success before commands start.
Combined fixture totals contain all three approaches; they are not an estimate
of incremental production Flash cost. Compilation and `.su` frames do not
establish device cycles or full call-chain stack depth.

The retained [12-image relation](../resources/evidence/README.md) separately
checks the existing firmware paths; `.bin` equality and ELF hashes are reported
independently. No board is accessed by either ergonomics runner.
