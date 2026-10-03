# Telemetry qualification suites

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.

These are the maintained C++20 suites for the final `telemetry` library.
The directory name records their development sequence; it does not identify
a second public library. Current includes use `telemetry/Telemetry.hpp`,
providers use `resource/telemetry/v3`, and the JS codec is `web/telemetry.js`.

The [implementation plan](../../doc/StructuredTelemetryV3ImplementationPlan.md)
and [migration guide](../../doc/StructuredTelemetryV3MigrationGuide.md) define
the contracts. The [checkpoint history](HISTORY.md) preserves earlier results
at their actual commits. An older green CI or receipt does not qualify a
later source tree.

| Suite | Evidence |
| --- | --- |
| reflection, facade | Pinned PFR/compiler probes, normalized reflection boundary, callable rules |
| types | Supported fixed-width shape, enum dictionary/code semantics and compile refusals |
| codec | Canonical LE, bool validation, exact lengths, object lifetime and Workspace |
| registry | Compile-time deduplication and structural type identity |
| service, model | Native bindings, local/global/encoded routing and ABI controls |
| endpoints | Mixed scalar/enum/array/struct Fields and Commands, storage budgets and codegen |
| descriptor | Independent binary parser, golden bytes and fingerprint |
| resources | Streaming/packed DescriptorFile, ValuesFile and application resource facade |
| traversal | Ordered metadata access, visit/container and runtime readAs/writeAs |
| exchange | Optional Bind/Exchange consumer framing and refusal before callbacks |
| client | JS/C++ interoperability, exact BigInt values and browser behavior |
| qualification | Multi-TU consumer, retained ARM symbols, bounds, leaf/direct codegen |
| mcu | Actual probe bodies on host, ARM O2/Os/Og compile/link and seven-role receipt |
| freeze | API/type/status/ceiling/dependency and immutable v3 byte contracts |

Each runner keeps commands and logs in the selected `--build-dir`. Expected
compile failures require their intended diagnostic. ARM runners inspect
linked symbols, sections, instruction streams and individual `.su` frames;
they execute zero ARM conditions. Host conditions, compiler commands,
declaration checks and mutation controls are reported separately.

```sh
python tests/structured/endpoints/run.py --cxx g++ --build-dir build/endpoints
python tests/structured/endpoints/run.py --cxx clang++-18 --sanitize --build-dir build/endpoints-san
python tests/structured/endpoints/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/endpoints-arm
python tests/structured/client/run.py --node node --build-dir build/client
python tests/structured/mcu/receipt.py verify tests/structured/mcu/local-receipt.json --self-test
python tests/structured/mcu/h7s/verify.py --self-test
```

The [H7S runner](mcu/h7s/README.md) requires an explicit selected device,
builds every image before access, checks its internal-Flash load span, and
backs up/restores all 64 KiB with a fresh readback. The current
[receipt](mcu/h7s/receipt.json) and [results](mcu/h7s/RESULTS.md) are distinct
from the [pre-unification proof](../../doc/evidence/pre-unification/README.md).
The independent PSP experiment measures observed stack writes across the
complete probe chain; it is not an unconditional maximum stack bound.

Legacy Scalar/v2 executable tests were retired after the baseline was
captured. Relevant ID/numeric/lifetime/slot/compiler controls remain in
`tests/regression`, generic filesystem checks in `tests/resources`, and
mixed endpoint/codec/wire checks here. The tracked review archive remains
historical; untracked review work was not modified.
