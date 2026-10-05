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
