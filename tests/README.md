# Telemetry checks

The active checks use the final C++20 telemetry API. Core tests do not require
Qt. The resource, protocol and client suites have separate runners; the CI
matrix also builds Cortex-M7 objects and the Qt playground and client.

Run the shared ID, slot and numeric checks from the repository root:

```sh
python3 tests/run_checks.py --cxx g++ --build-dir build/checks-gcc
python3 tests/run_checks.py --cxx clang++-18 --build-dir build/checks-clang
python3 tests/run_checks.py --cxx g++ --null-checks --build-dir build/checks-null
python3 tests/run_checks.py --cxx clang++-18 --sanitize --build-dir build/checks-sanitized
python3 tests/run_arm_checks.py --build-dir build/arm
python3 tests/run_arm_checks.py --null-checks --build-dir build/arm-null
```

`--source-root PATH` selects a library checkout for preparation runs while
the fixtures remain beside the runner. It defaults to the repository containing
the runner. The shared tests link no compiled telemetry adapter or serializer.

Every command keeps its arguments, exit status and diagnostic in a log. Each
build directory also contains `summary.json` with actual command, intended
refusal, execution, runtime-check and intentional-abort counts. Runtime counts
come from executed fixtures. Compiler-only checks do not claim runtime coverage.

The shared coverage preserves:

- Packed ID width and signedness before narrowing, local enum positions,
  rejected implicit ID wrappers and explicit template-type narrowing,
  using/ADL name controls, and intentional failures at runtime and before main.
- All five stable slot forms, ownership and destruction, exact capacity and
  alignment, replacement of an aliased owned target, reference forwarding,
  callable overload selection, exact signatures, and no heap fallback for
  owning slots even when the companion delegate permits one.
- Hand-derived numeric boundaries and constexpr/runtime parity, unchanged
  destination on refused conversion, signed zero, subnormal and NaN/Inf cases,
  and the independent extended-precision numeric oracle. The oracle explicitly
  reports a skip when the platform has fewer than 64 long-double mantissa bits.
- Standalone `.h` and `.hpp` headers, null-check compiler modes, ELF weak targets
  and strong overrides, `-Og` compilation, and unsupported floating-point modes.
- Cortex-M7 high-word rejection before ID use, native-width extraction without
  added work, and controls that reject a changed high-word register.

The ARM shared runner compiles objects and checks diagnostics and disassembly.
Its `.su` files describe individual compiler frames. It does not execute the
firmware or establish full call-chain stack usage. The dedicated
[MCU qualification](structured/mcu/README.md) and frozen typed-core runners
carry their separate runtime, codegen, frame, ABI and evidence gates.

[Structured suites](structured/README.md) retain native mixed endpoints,
reflection facade checks, types, codec/workspace lifetime, registry, services,
model, descriptor and values goldens, resources, traversal, optional protocol,
client interoperability, multi-TU qualification, MCU qualification and the
frozen API/wire contract. [Resource checks](resources/README.md) preserve the
independent flat filesystem, lazy `FileView`, borrowed `BytesFile` and
`resource::protocol` checks without telemetry dependencies. The optional
[v3 provider checks](structured/resources/README.md) validate descriptor/value
providers and the application's resource facade separately.

[User-guide checks](docs/README.md) compile the complete native, resource and
encoded examples linked from the [public guide](../doc/user/README.md).
Host mode executes the examples with assertions enabled; ARM mode compiles
and links them without running an image on a device.

Scalar, legacy Field metadata/limits/arguments, JSON adapter, binary v2 and old
ABI suites are retired after their source and measured evidence are archived.
The fourteen obsolete `telemetry_*check.pro` targets referenced removed legacy
sources and are retired too. Their original files remain in Git history and the
local cleanup archive; the active qmake targets live under `tests/structured/`.
Their historical counts and receipts are evidence for those source snapshots;
they do not describe the final C++20 test matrix. The final API has no source,
ABI or wire compatibility promise with v2.1. See the
[migration guide](../doc/StructuredTelemetryV3MigrationGuide.md).
