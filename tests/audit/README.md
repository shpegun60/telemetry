# Library audit after the positional-table refactor

Baseline: `9f5951814697074eeed5855bd302cc694caa5790`.
Scope: every header/source and the qmake include under `lib/telemetry`, plus
public API documentation, regression runners and generated ARM code. Bundled
third-party libraries were exercised as dependencies, not independently audited.

## Findings and repairs

| Area | Reproduced problem | Repair and regression |
| --- | --- | --- |
| Explicit enums | An unscoped `enumSpec` with code 1000 was accepted by metadata but rejected by both field write paths; an entirely out-of-scan enum could fail to compile. | Carry the selected constraint type through native and erased adapters. Test free functions, methods, borrowed closures, signed/unsigned enums, subsets, interval gaps and direct setters. |
| Null targets | Null member/context pointers passed invocability traits and could be called. | Reject null NTTP targets at compilation; empty runtime Getter/Setter values retain their documented behavior. |
| Callback conversion | Paired parameter callbacks admitted a throwing pointer conversion; forwarding wrapper objects by value also made unnecessary copies inside the factory. | Validate both actual conversions and immediately pass exact pointers to the binding layer. |
| C++20 adapters | A structural setter could convert to a noexcept pointer but invoke a throwing operator; context traits checked an rvalue although the adapter is called as a const lvalue. | Validate the actual invocation expression and value category. Preserve supported nonthrowing structural adapters. |
| Borrowed lifetime | Explicit const/reference template arguments could bind temporary field callables or field/command owners. A field reproduction triggered ASan stack-use-after-scope. | Reject those overloads at compilation while retaining stable const and nonconst lvalues. Internal borrowed command metadata receives the same protection. |
| Floating mode | MSVC `/fp:fast` bypassed the unsupported-mode guard; the reproduction converted a double NaN into float infinity. | Reject `_M_FP_FAST`, alongside GCC/Clang fast/finite-math modes. Normal precise-mode conversion instructions are unchanged. |

Library comments explain borrowed ownership, active union members, conversion
and validation order, template routing, JSON buffer/hash contracts and ABI
requirements. They intentionally do not annotate obvious individual statements.

## Coverage

- Core/numbers: all 11 numeric alternatives, exact identity, cross-type bounds,
  floating endpoints, nonfinite values, signedness and failure without mutation.
  The independent numeric oracle additionally enumerates every bool, U8, S8,
  U16 and S16 source value against all 11 destinations: 1,447,446 inputs.
- Fields/bindings: direct functions, lambda pointer forms, borrowed closures,
  const/derived owners, explicit enums, native/Scalar parity and no side effect
  on rejected writes. C++20 adapter value categories receive separate checks.
- Commands: count/type/range rejection, explicit enum intervals, all numeric
  alternatives, virtual/derived owners, ref-qualified closures, metadata
  ownership and local/global/native/erased dispatch.
- Catalog/JSON/ABI: dense positional bounds, null/empty views, buffer failure,
  numeric extrema, schema independence from owner address/current values,
  matching archives and rejected mixed layouts. JSON expects valid UTF-8 input;
  borrowed strings and raw pointer/count views retain caller preconditions.

Run the [host and ARM checks](../README.md) on the final tree. Host tests run
with warnings as errors and optionally ASan, UBSan, float-cast-overflow and
stack-use-after-scope/return checks. CI also builds C++17/C++20 with GCC/Clang,
the Qt playground and the Cortex-M7 consumer.

The runner verifies 209 rejected programs in C++17 and 212 in C++20, including
[borrowed fields](../TelemetryBorrowedFieldCompileFail.cpp),
[command lifetime](../TelemetryCommandLifetimeCompileFail.cpp) and
[structural adapters](../TelemetryAdapterCompileFail.cpp). Accepted lvalue and
inline callback forms are tested alongside the lifetime rejections.

Additional local MSVC 19.50 C++17 `/O2 /fp:precise` checks use an 8 MiB host
test stack. With MSVC's forced inlining, the ReadCheck `checkAccess` frame was
1,336,832 bytes and exceeded the default 1 MiB reserve; relinking the same
objects with `/STACK:8388608` passed. This is compiler-specific host test
behavior, not an ARM stack measurement. Intentional alignment padding and
constant-folding/shadow-name diagnostics require `/wd4324 /wd4702 /wd4756 /wd4459`; the host CRT
also uses `_CRT_SECURE_NO_WARNINGS`. The independent numeric oracle is skipped
on MSVC because its `long double` has only 53 mantissa bits; GCC/Clang perform
the full oracle checks. These accommodations do not relax runtime validation.
With these settings the local MSVC pass completed 1,131 assertions across the
nine applicable suites and rejected all 53 newly added C++17 misuse cases.

## ARM comparison

Build the baseline and current source with the same CubeIDE compiler and run
`tests/run_arm_checks.py` for both. Then compare their output directories:

```sh
python tests/audit/compare_arm_probes.py --before build/baseline-arm --after build/current-arm --output build/arm-comparison.json
```

The comparison checks encoded instruction words and literal pools, excluding
addresses, labels and relocation-only lines. It is an additional regression
check, not a proof of arbitrary program equivalence; the ARM runner separately
checks call targets, native dispatch, constant storage and layout. The baseline
and repaired code preserve the 12 existing probes at both `-O2` and `-Os`.
Field remains 96 bytes/aligned 32 and Command 20 bytes on ARM32; ABI remains 6.

No new board timing claim follows from this audit. Callback synchronization,
valid borrowed lifetimes, aligned storage and a consistent build configuration
remain application responsibilities. Passing these checks establishes the
tested contracts, not mathematical absence of all possible defects.
