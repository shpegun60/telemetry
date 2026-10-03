# Archived pre-unification checkpoint history

Preserved from `389c995`. Paths, statuses and measurements below describe
their original commits; use [the current index](README.md) for active API/tests.

# Structured telemetry implementation checkpoints

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.

The implementation contract is
[StructuredTelemetryV3ImplementationPlan.md](../../doc/StructuredTelemetryV3ImplementationPlan.md).
This directory records evidence as each stage is implemented. The structured
library is not present at the Stage 00 checkpoint.

The latest [Stage 13 qualification](qualification/README.md) was published as
`7266a93` and passed exact-SHA CI 37122588689, all 9/9 jobs.
[Stage 14 probe preparation](mcu/README.md) continues host execution and ARM
disassembly without a board. The user deferred new hardware measurements;
this preparation does not close the MCU execution gate.

## Stage 00: scalar baseline

Tracked scalar source: `5a289a8f12a85f68219275abcde7b58d925076ac`,
"Close telemetry follow-up contract and debug gaps". Local `HEAD`,
`origin/main`, and `git ls-remote origin refs/heads/main` all identified this
commit before the first structured change. The source files tracked by Git
were unchanged. The two implementation plans and `tests/review-2026-09-26/`
were separate untracked material; the review tree was not included in the
baseline or modified here.

[Exact-SHA CI run 36253273669](https://github.com/shpegun60/telemetry/actions/runs/36253273669)
completed successfully: all nine jobs, including GCC/Clang C++17 and C++20,
the Clang sanitized job, Cortex-M7 compile/link, and Qt. This prior run is
the software gate for the existing scalar code; no new local full-suite result
is claimed for Stage 00.

The current scalar ABI is revision 8 in
[`TelemetryAbi.h`](../../lib/telemetry/abi/TelemetryAbi.h).
The existing ARM instruction and storage gates are
[`run_arm_checks.py`](../run_arm_checks.py),
[`FieldTableCodegen.cpp`](../FieldTableCodegen.cpp),
[`CommandTableCodegen.cpp`](../CommandTableCodegen.cpp), and
[`IndexCodegen.cpp`](../IndexCodegen.cpp). Their source remains the frozen
comparison point for the new module. The resource checks and binary v2.1
adapter are part of the same CI run.

The CI ARM compiler was `arm-none-eabi-g++` 13.2.1. Its retained artifact
`cortex-m7-check-logs` contains 44 disassemblies and 18 stack-usage files.
The committed [baseline data](baseline-arm-gcc13.json) preserves normalized
instruction streams for ten representative Field/Command/Index symbols at
`-O2` and `-Os`, along with linked resource section sizes and maximum
per-function stack frames. The complete CI logs can be retrieved with:

```powershell
gh run download 36253273669 --name cortex-m7-check-logs --dir build/structured-baseline/ci-arm-logs
python tests/structured/capture_scalar_baseline.py --logs build/structured-baseline/ci-arm-logs --source-sha 5a289a8f12a85f68219275abcde7b58d925076ac --ci-run https://github.com/shpegun60/telemetry/actions/runs/36253273669 --output tests/structured/baseline-arm-gcc13.json
```

| Optimization | Linked resource `.text` | `.rodata` | `.data` | `.bss` | Maximum Schema/Commands frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| `-O2` | 29,372 B | 2,336 B | 116 B | 600 B | 176 B |
| `-Os` | 20,156 B | 2,272 B | 116 B | 600 B | 160 B |

These are sections of the CI **resource fixture**, not the H753 firmware
image. Stack figures are individual frames, not the sum of a call chain.
For a future local comparison, use the same compiler and fixture; the
installed CubeIDE compiler is a different version (14.3.1).

The archived [H7S review receipt](../regression/h7s/current-receipt.json)
was checked with `verify.py --current-code --self-test`. All 77 recorded
library input hashes matched this source; the receipt also passed its 11
mutation controls. Its O2/Os run and restoration remain **archived** evidence,
not a new board measurement at this checkpoint.

Available tools inspected at Stage 00: Python 3.13.14, Qt MinGW GCC 13.1.0,
WSL Clang 18.1.3, and CubeIDE ARM GCC 14.3.1. New structured probes should
record the compiler and flags they actually use.

## Stage 01: C++20 reflection backend

The unmodified Boost.PFR 1.92.0 header tree and its Boost Software License
are in [`lib/boost_pfr`](../../lib/boost_pfr/VERSION.md). It is pinned to
upstream commit `401385c240027423acbb1eb6dea2abe0043db5aa`. Only PFR was
copied: 44 headers and the license. Existing
`magic_enum` remains pinned at v0.9.8. The PFR source bytes are not a
measurement of linked firmware size.

[`PfrProbe.cpp`](reflection/PfrProbe.cpp) uses PFR's C++20 names and
structured-binding engine for two scalar fields, repeated types, a nested
aggregate, `std::array`, mutable/const reference access and names that are
identical in a second translation unit. It exercises ordinary and signed
enums, a sparse value outside `magic_enum`'s default scan, and numeric aliases.
The automatic sparse scan finds only `None`; an explicit
`enum_name<SparseMode::Far>()` does find `Far`. Aliases share a numeric code;
the selected name is compiler-dependent. The probe rejects a non-ASCII
automatic member name and accepts an explicit UTF-8 string as data. The
actual public-name validation belongs to the facade stage.

Run the focused checks with the vendored headers:

```text
python tests/structured/reflection/run.py --cxx g++ --build-dir build/structured-pfr-gcc
python tests/structured/reflection/run.py --cxx clang++-18 --build-dir build/structured-pfr-clang
python tests/structured/reflection/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-pfr-arm
```

The runner records each compiler command, diagnostic and elapsed time in the
chosen build directory. The negative source must fail for its specific ASCII
diagnostic. CI runs the host C++20 probe on GCC and Clang and the ARM probe
at `-O2`, `-Os` and `-Og`.

| Local compiler | Probe result | Typical positive compile time per unit | ARM object sections, O2/Os |
| --- | --- | ---: | --- |
| Qt MinGW GCC 13.1.0 | host link/run and negative check pass | 0.5–0.8 s | — |
| Ubuntu GCC 13.3.0 | host link/run and negative check pass | 0.7–1.1 s | — |
| Ubuntu Clang 18.1.3 | host link/run and negative check pass | 0.7–1.2 s | — |
| ARM GCC 13.2.1 | O2/Os/Og compile and negative check pass | 0.6–1.0 s | Probe 150 B `.text`, Other 4 B `.text`; zero `.data/.bss` |
| CubeIDE ARM GCC 14.3.1 | O2/Os/Og compile and negative check pass | 0.6–1.0 s | Probe 146 B `.text`, Other 4 B `.text`; zero `.data/.bss` |

The ARM size rows describe the **test objects**, which include `main` and
`printf`; they do not isolate PFR overhead or predict the final firmware.
No STM32 board run is claimed for this header-only compiler probe. Stage 02
can now build the stable reflection facade on this verified backend.

## Stage 02: stable reflection facade

The new [`reflection`](../../lib/telemetry_structured/reflection) namespace
contains the sole aggregate and callable traits interface for future
Registry/Codec/Model code. Only `reflection/detail/PfrAdapter.hpp` includes
Boost.PFR; direct vendor symbols are forbidden outside the backend adapter.
`Aggregate.hpp` exposes `memberCount`, exact `MemberType`, `memberName` and
`get`. The facade validates compiler-derived member names as ASCII
identifiers. A mutable or const lvalue keeps its member reference and cv.
PFR copies a member when passed a temporary aggregate; the facade rejects
that call so member access remains reference based and copy free.

`Callable.hpp` provides `Function<Signature>` facts for free functions,
function pointers, member functions with all cv/ref/noexcept combinations,
unique callable objects and the four existing slot families. Its
`EndpointTraits` validates the common shape before normalizing one request
and unwrapping a `ServiceResult<Response>` return. It preserves the original
Result and Arguments for factory-specific checks. Only Service has a
`Response` payload; CommandResult and WriteResult remain operation statuses.
`ServiceResult` storage is deferred to Stage 06; these traits need only its
declared type. Stage 02 established the `Enum.hpp` specialization point;
Stage 03 supplies the normalized dictionary.

[`facade/run.py`](facade/run.py) checks a host executable and a second
translation unit, 14 distinct compile-time rejection reasons, and the
source-level vendor boundary. ARM runs compile at `-O2`, `-Os` and `-Og`.
It passed locally on Qt MinGW GCC 13.1, Ubuntu Clang 18.1, ARM GCC 13.2 and
CubeIDE ARM GCC 14.3. The CubeIDE O2/Os test objects have 20/18 total bytes
of `.text`, respectively, and no `.data/.bss`; these are small facade
fixtures, not a firmware-size comparison. The new CI steps run the same
checks on host GCC/Clang and Cortex-M7. They do not claim a board run.
The exact-SHA CI run
[36274996182](https://github.com/shpegun60/telemetry/actions/runs/36274996182)
completed 9/9 jobs successfully for
`a9d445db0d640fcfabdb1c540ee1b27efe09db83`, closing the Stage 00–02
checkpoint before Stage 03 changed the library.

## Stage 03: fixed wire types and enum dictionaries

[`types/run.py`](types/run.py) checks two positive probes and 43 distinct
compile-time rejection cases. The positive checks cover all eleven scalar
codes, fixed-size arrays, empty and nested aggregates, zero-byte types,
checked wire-size and expanded-node budgets, default-underlying enums,
ordinary and sparse enums,
aliases, an explicit empty dictionary, signed ordering, exact UTF-8 names,
and a name copied from a local char array into the constexpr definition.
The negative matrix covers unsupported C++ shapes, enum dictionary mistakes,
borrowed-name impostors, invalid UTF-8, range/depth budgets and common packed
layouts. Each case must
fail with its intended diagnostic; a mere nonzero compiler exit is insufficient.

Run the focused checks with:

```text
python tests/structured/types/run.py --cxx g++ --build-dir build/structured-types-gcc
python tests/structured/types/run.py --cxx clang++-18 --build-dir build/structured-types-clang
python tests/structured/types/run.py --cxx clang++-18 --sanitize --build-dir build/structured-types-sanitized
python tests/structured/types/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-types-arm
```

The current local matrix passed with Qt MinGW GCC 13.1, Ubuntu Clang 18.1,
Ubuntu ARM GCC 13.2, and CubeIDE ARM GCC 14.3. The ARM probes compile at
`-O2`, `-Os` and `-Og`, with exceptions and RTTI disabled. The host Clang
probes also ran under address and undefined-behavior sanitizers. This is
compile-time type metadata and does not claim an MCU cycle measurement or a
board run.

One toolchain difference changed the implementation: on the ARM GCC targets,
`std::int32_t` can alias `long` while `int` is also a 32-bit signed type.
The scalar classifier therefore uses checked width and signedness, excluding
plain `char` and character types, rather than the spelling of a typedef.
Similarly, GCC's PFR rejects typical packed fields while Clang may accept
them. An explicit alignment check rejects the tested whole-struct and
member-level packed forms. Packed C++ aggregates remain outside the contract;
no C++20/PFR trait proves the absence of every packing attribute, especially
with nontrivial default member initialization. The ordinary aggregate wire
size is already padding-free.

## Stage 04: codec, Workspace and object lifetime

[`codec/run.py`](codec/run.py) runs independently specified golden byte
vectors for all scalar widths, signed minima, FP bit patterns, unknown enum
codes, nested aggregates and arrays. It rejects every truncated length, a
trailing byte, invalid `bool` codes 2/255, and overlapping buffers. Separate
probes cover skewed and exact-size Workspace buffers, RAII destruction,
unchanged DMI side-effect counts, and a 4 KiB value with allocation disabled.
The negative compile check rejects `ServiceResult<Big>::success(Big{})` and
points callers at the bounded-stack factory form. Another negative check
rejects a DMI array that would expand more than 1024 initializer nodes.

```text
python tests/structured/codec/run.py --cxx g++ --build-dir build/structured-codec-gcc
python tests/structured/codec/run.py --cxx clang++-18 --sanitize --build-dir build/structured-codec-sanitized
python tests/structured/codec/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-codec-arm
```

The local correctness checks passed on Qt MinGW GCC 13.1 and Ubuntu Clang 18.1
with address/undefined-behavior sanitizers. CubeIDE ARM GCC 14.3.1 compiled
the fixtures with `-O2`, `-Os`, and `-Og`; this is a compiler/codegen check,
not an MCU cycle or board measurement. The `.su` figures below are individual
stack frames, not a whole call-chain bound:

| CubeIDE ARM frame | `-O2` | `-Os` | `-Og` |
| --- | ---: | ---: | ---: |
| 4 KiB `encode_big` | 16 B | 24 B | 16 B |
| 4 KiB `decode_big` | 32 B | 40 B | 48 B |
| `make_actual_service` via `successFrom` | 8 B | 8 B | 16 B |
| Placement of returned `ServiceResult<Big>` | 8 B | 8 B | 8 B |
| `std::construct_at(storage, make_raw())` comparison | 4104 B | 4104 B | 4112 B |
| `std::optional<Big>{make_raw()}` comparison | 4112 B | 4112 B | 4120 B |

The production code uses direct placement construction from a prvalue, not
`construct_at` or an optional payload. `ServiceResult` has private status and
active payload storage. `success(value)` is restricted to objects of at most
256 bytes; `successFrom(factory)` is the large-response form. This is an
explicit API choice based on return ABI evidence, and Stage 06 must rerun the
same gate on the final service thunk.

For the 4 KiB homogeneous array, the looped fixture's entire `.text` is
680/662 B at O2/Os; the template-expanded comparison is 26,524/10,238 B.
The DMI aggregate containing a 4 KiB array also keeps a small stack frame
(48/56/48 B at O2/Os/Og) and uses a loop after explicitly constructing all
members. This path currently initializes its large array to zero before
overwriting it, a correctness/code-size tradeoff for nontrivial default
construction. Ordinary trivially default-constructible arrays take the direct
loop path without that preliminary fill.

This stage does not yet claim an encoded Field/Command/Service dispatch path;
those endpoints and the model are later stages.

## Stage 05: compile-time TypeRegistry

[`registry/run.py`](registry/run.py) verifies the fixed TypeIds for Void and
eleven scalars, dependency-first registration, exact C++ type identity,
deduplication across twenty uses, struct member names/types, enum code bits,
checked runtime lookup, and independently expected type-record lengths. A
second translation unit checks the same order and names. Two negative cases
reject an absent type and an out-of-range compile-time ID. Three link controls
change the structured ABI revision, a descriptor size, and an offset; all
must fail to resolve the exact ABI tag.

```text
python tests/structured/registry/run.py --cxx g++ --build-dir build/structured-registry-gcc
python tests/structured/registry/run.py --cxx clang++-18 --sanitize --build-dir build/structured-registry-sanitized
python tests/structured/registry/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-registry-arm
```

Locally, Qt MinGW GCC 13.1 and sanitized Ubuntu Clang 18.1 passed. CubeIDE
ARM GCC 14.3.1 and Ubuntu ARM GCC 13.2.1 compiled and linked at `-O2`,
`-Os`, and `-Og`, including all ABI rejection controls. The ARM fixture's
registry storage was emitted into `.rodata` with no `.data` or `.bss`; its
linked `.text/.rodata` was 144/964 B at `-O2` and 132/954 B at `-Os` on
CubeIDE GCC 14.3.1. These sizes describe the small Stage 05 fixture, not
the firmware or a complete descriptor serializer. No board cycles are
claimed. Exact-SHA CI run
[36308779553](https://github.com/shpegun60/telemetry/actions/runs/36308779553)
completed successfully (9/9 jobs) for
`4f159742e5d2e0806abe115c02da5a7f5a2cfb21`.

The registry currently takes ordered root types as template arguments.
Stage 08 will derive this sequence from Field, Command and Service tables;
users of the final Model will not maintain a second type list. Stage 05
produces structural records and their checked sizes, not `descriptor.bin`
bytes or a fingerprint. The type-record budget is checked here; the full
descriptor budget still needs the catalogs and endpoints from later stages.

## Stage 06: native Service binding

[`service/run.py`](service/run.py) checks direct functions and methods,
by-value and reference requests, raw and wrapped response/void returns, all
Service statuses, runtime function pointers, capturing lvalues, all five slot
families, empty slots with zero callbacks, rebinding, and one target snapshot
per call. Twenty compile-fail cases cover invalid signatures, temporary or
proxy owners, temporary stateful callables, extra metadata, and null NTTP
targets. An ELF-only fixture checks absent weak declarations, weak bodies and
strong overrides. The ARM fixture checks the real native Service wrapper with
a 4 KiB response and inspects the direct-owner call for null branches in
ordinary `-O2`/`-Os` builds.

```text
python tests/structured/service/run.py --cxx g++ --build-dir build/structured-service-gcc
python tests/structured/service/run.py --cxx clang++-18 --sanitize --build-dir build/structured-service-sanitized
python tests/structured/service/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-service-arm
```

The CubeIDE ARM GCC 14.3.1 checks passed at `-O2`, `-Os` and `-Og`, including
`-fno-delete-null-pointer-checks`. For the real 4 KiB Service call, individual
`call_big_raw` frames were 16/16/24 B and `call_big_wrapped` frames were
16/16/8 B at O2/Os/Og. They are not whole-call-chain or board-cycle results.
On ARM GCC 13.2.1 the 4 KiB `ServiceResult` layout was 4097 B, alignment 1;
the direct free-function and direct-owner Service definitions were each 8 B,
alignment 4. The runner reads these measurements from the ARM object itself.
The ordinary direct-owner fixture has no null branch at O2/Os; null-check
mode can retain a check for the target function address, separate from the
owner address.

## Stage 07: Service tables and first encoded Model path

[`model/run.py`](model/run.py) builds one `ReadCalibration` service through
local typed, global typed and encoded routes. Additional probes cover two
catalog groups, enum local positions, wide runtime IDs, application versus
dispatch `Unavailable`, void request/response, all five late-bound slot families,
one target snapshot, overlapping
buffers and zero callbacks on short output, short workspace or malformed
input. The 4 KiB response fixture checks that encoded dispatch places the
result in caller-owned Workspace. Thirteen compile-fail cases cover invalid
positions, copying a self-referential table, temporary table/model views and
explicit ID-template bypasses. Three mutations of the exact ABI tag fail to
link against the real compiled encoded adapter on host and ARM; compatible
adapters link on ARM at `-O2` and `-Os`.

```text
python tests/structured/model/run.py --cxx g++ --build-dir build/structured-model-gcc
python tests/structured/model/run.py --cxx clang++-18 --sanitize --build-dir build/structured-model-sanitized
python tests/structured/model/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-model-arm
```

On CubeIDE ARM GCC 14.3.1 at `-O2` and `-Os`, the direct owner, local table and
global catalog typed probes have identical machine bytes: 24 B and 20 B per
function, respectively. Their stack frames are 0 B. The encoded entry
wrapper uses 48 B. The 4 KiB encoded thunks use 48/48 B at `-O2`, 104/112 B
at `-Os`, and 80/80 B at `-Og`. These are individual frames from `.su`, not
whole-call-chain peaks or board-cycle measurements. An early `-Os` probe had
an extra method call through `std::invoke`; direct member invocation removed
it without changing the public API.
The CI ARM GCC 13.2.1 toolchain also produced identical native call bytes
(24 B at `-O2`, 20 B at `-Os`); its 4 KiB encoded thunk frames were 80/80 B,
104/104 B and 80/80 B at `-O2`/`-Os`/`-Og`.

Stage 07 exposes a working Service without a descriptor file or UI. The
Field/Command families and the shared mixed catalog were added in Stage 08 below.

## Stage 08: mixed Field, Command and Model

[`endpoints/run.py`](endpoints/run.py) checks one table with bool, u16, float,
double, enum, array, struct and read/write struct, all retaining exact native
types. The same MotorConfig in Field, Command and Service has one TypeId.
Tests cover typed/encoded parity, every status, read-only and unavailable
bindings, all five slot families, one snapshot, u32 routing, short/overlapping
buffers, nested leases, 4 KiB no-heap endpoints, DMI suppression and malformed
bool rejection before callbacks. LeafStorage covers every scalar width and
enum with local/Workspace storage selected at compile time.
CodecParity checks skewed buffers, NaN payloads and unknown enum codes.
ErasedBoundary checks direct contexts (including different read/write objects,
const/derived owners and OwnerSlot rebinding), checked entry methods, and
Service input/output sharing. Complete and partial overlap in either direction
is exercised for local, mixed and Workspace request/result storage. Invalid
lengths/bools do not call the endpoint, and failed Services leave output intact.

[`endpoints/storage.py`](endpoints/storage.py) tests budgets 0/16/32/64,
exact and one-byte-over boundaries, 4 KiB objects, aligned DTOs with skewed
Workspace spans, mixed local/Workspace Service request/result placement,
wrapper-inclusive size accounting, short scratch, DMI and nested leases.
It also compiles a real caller with budget 0 against adapter/ABI TUs with
budget 16 and requires the exact adapter symbol to fail linking.
Host: 60 successful commands and two expected rejections. ARM: 88 successful
commands and two expected rejections, across O2/Os/Og. These are generated
runner totals; sanitizer host executes the same storage matrix.

Forty compile-fail cases check signatures, semantic-metadata rejection,
temporary owners/views, local/global positions and explicit-template bypasses.
Cases 37–40 specifically reject temporary groups/catalogs hidden by explicit
types or braces; all four compiled against the pre-guard snapshot. These
guards change construction validity, not runtime dispatch instructions.
Three exact-ABI mutations must fail to link against real compiled adapters.
Counts are computed by the runner: Linux host has 19 successful commands and
43 expected failures; ARM has 110 successful commands and 43 expected failures
without null-check mode (which intentionally skips native byte equality).

```text
python tests/structured/endpoints/run.py --cxx g++ --build-dir build/structured-endpoints
python tests/structured/endpoints/run.py --cxx clang++-18 --sanitize --build-dir build/structured-endpoints-sanitized
python tests/structured/endpoints/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/structured-endpoints-arm
```

Locally verified with Clang 18 ASan/UBSan, GCC null-check mode and CubeIDE
ARM GCC 14.3.1 at O2/Os/Og. The eleven direct/local/global native comparisons
are byte-identical at O2/Os. Tables stay in read-only storage without startup
constructors. ARM runtime entries are 28/20/24 B for Field/Command/Service,
alignment 4. The current 4 KiB read/write/command thunk frames are 24/12/12 B at O2,
24/16/16 B at Os and 80/72/72 B at Og; these are individual frames,
not complete call-chain peaks.
The local CI-toolchain replay with ARM GCC 13.2.1 also retained all eleven
native byte-equality checks. Its large endpoint frames were 24/12/12 B at O2,
48/48/48 B at Os and 80/72/72 B at Og; compiler-specific frame figures are
not interchangeable with the CubeIDE measurements.

The shared codec optimization was also checked with the unchanged Stage 04
and Stage 07 probes. Service native instructions are unchanged. Its 4 KiB
encoded frames after the storage/preflight changes are 32/32 B at O2,
40/40 B at Os and 64/64 B at Og on CubeIDE 14.3.1. Both raw and wrapped
4 KiB responses remain in Workspace. The one-byte Request fits the local
budget; the full call-chain bound is still separate from these thunk frames.

Actual H7S runs cover correctness, natural/8/32 alignment, same-image legacy
comparison and component costs. See [results and receipts](endpoints/h7s/README.md).
The new encoded path is not claimed universally faster than the old scalar
adapter. Stage 13 still owns full call-chain stack/no-heap linked evidence.

## Stage 09: descriptor v3.0 and fingerprint

Implemented immutable structural records, one constexpr FNV-1a fingerprint,
indexed streaming reads and optional packed Flash bytes. Fields/Commands/
Services remain name + binding; no semantic metadata was added.

Three independently specified goldens pass host and ARM extraction, including
UTF-8, empty shapes and one type shared across all endpoint categories.
The independent parser enforces all resource ceilings and has 1729 checks;
the C++ runner adds 22 diagnostic-specific negative cases, no-heap controls,
every-offset/chunk reconstruction and cross-TU checks. Host sanitizers,
null-check mode and both ARM GCC 13/14 toolchains pass locally.

The prior `45652e7` CI's single ARM failure was traced to GCC 13 `-Os`
sharing a native Service wrapper with erased dispatch. Separate instantiations
of the same implementation restore the unchanged native byte-equality gate;
no blanket forced inlining or relaxed assertion was introduced.

H7S same-image measurements favor packed bytes for constexpr metadata.
The board was restored and verified. Exact byte fixtures, sizes, stack frames,
cycles, reproduction commands and scope limits are in
[descriptor/README.md](descriptor/README.md). Resource provider integration
was completed in Stage 10 below. This slice does not start Bind/Exchange or UI work.

## Stage 10: resource providers and dense values

`DescriptorFile` now wraps packed or streaming metadata and `ValuesFile`
emits fixed-size native snapshots through existing resource READ. Following
the user's Stage 10 decision, its 24-byte header includes the cached u64
descriptor fingerprint. No live-value hashing or per-chunk fingerprint is
added. `ValuesFile{descriptor, workspace}` obtains fields and identity from
the same source; `size()` never invokes getters.

The [provider suite](resources/README.md) covers 5700 cursor/capacity cases
at each storage budget, whole-token getter counts, scratch capacity/lifetime,
four qmake dependency selections, independent wire parsing, compile/link
rejections, sanitizers and two ARM toolchains. Actual H7S checks passed at
O2/Os and the original Flash was restored and read-back verified.

The previous `c25c6fa` CI failed because the legacy resource header sweep
included optional structured headers without their PFR dependency. Coverage
is now split into core/v2 headers and the explicit structured suite. The
unchanged v2 bytes/protocol tests remain enabled. Exact-SHA remote CI must
still be checked for the commit that publishes this stage.

## Stage 11: optional upper protocol example

`example::structured_protocol::Binding`, `Bind::process` and `Exchange::process`
connect a transport-owned peer to the existing immutable model. Data requests
carry 24-byte headers without fingerprints or session IDs. Field writes,
Commands and Services use the existing encoded entries and their validation.
Core endpoint definitions, ABI and descriptor/value wire formats are unchanged.

The [Stage 11 suite](exchange/README.md) covers malformed packet/capacity
matrices, all named endpoint statuses, native payload validation, aliases,
scratch lifetime, late binding, repeated requests, bounded peer admission,
disconnect/reboot/old queues and client correlation. Separate compile/link
controls retain both new compiled boundaries under GC and LTO. Independent
goldens fix the wire format; the full client remains Stage 12.

H7S passed 4300 checks in each of O2/Os and recorded paired encoded/Exchange
cycle windows. Those are different layers; packet dispatch is not claimed to
have the cycle cost of a local index call. Flash restoration was verified.

## API correction before Stage 12

The [protocol example](../../examples/structured_protocol/README.md) now lives
outside telemetry/resource. Libraries do not expose packet/session state and
neither `.pri` links Bind/Exchange. Core endpoint DispatchStatus retains its
existing operation codes; packet-only statuses belong to the example.
Descriptor/ValuesFile bytes and fingerprints remain unchanged. Their strict
independent parsers/oracles stay enabled.

Every endpoint family now has typed `get`, ordered `forEach`, O(1) runtime
native `visit` and erased `empty/begin/end/operator[]` iteration. Fields also
have static/runtime `readAs/writeAs`, with checked numeric conversion and
exact structural types, without Scalar or a model-wide owning variant.
The [traversal suite](traversal/README.md) records the 64 compile-fail contracts,
sanitizers, allocation controls, ARM direct-call comparisons, visitor frames
and 32/128-row code-size costs. Client work had not started at this checkpoint.

## Stage 12: client codec and desktop examples

The separate [JS client](../../web/telemetryStructured.js) parses immutable
structural models, validates descriptor fingerprints and decodes dense values.
It encodes Field writes and Command/Service requests and decodes exact native
responses, preserving U64/S64 as BigInt. The original v2 decoder is unchanged.
No transport, Bind, requestId or connection state is imported into the codec.

The [client suite](client/README.md) has 3057 counted checks, independent frozen
fixtures, semantic descriptor mutations, depth/expansion bounds and a valid
model reaching packed ID `0xffffffff`. JS/C++ byte exchange covers all three
endpoint families and exact callback/status behavior. Local MinGW, GCC null
checks and Clang ASan/UBSan passed. A real browser passed 20 checks including
timeout without retry, literal reflected names and precise integer64 display.

The [desktop example](../../examples/structured_client/README.md) supplies a
recursive Service form and a separate localhost HTTP fake. Qt C++20 smoke
uses the same Model/Workspace and preserves the exact 41-byte response.
This is desktop evidence, not new MCU cycles. Native library source hashes
still match the API-correction H7S receipt. Whole-program qualification and
final migration remain subsequent stages; publication CI is checked per SHA.

## Stage 13: integrated qualification

The [multi-TU suite](qualification/README.md) combines native and encoded
Field/Command/Service access, immutable resources and 4 KiB DTOs. It runs 97
counted conditions per mixed consumer executable; repeated optimization,
GC/LTO/PIC/PIE and scaling runs total 2433 checks on MinGW and 2635 on Linux.
Compile rejections, link rejections and runtime checks have separate counters.
CubeIDE GCC 14.3.1 and ARM GCC 13.2.1 inspect O2/Os/Og linked sections, frames,
no-heap symbols and known-position u32 direct-call bytes. Null-check modes
include constexpr array-element pointer names across all endpoint families.

The suite reproduced a constructor-only GCC null-check gap on `cd8b636`.
Name now performs the shared pointer/UTF-8 validation once; endpoint/group
constructors consume that validated wrapper. No hot invocation, wire format,
descriptor layout, local budget or endpoint result contract changed. Original
Stage 07/08 native direct/local/global gates remain intact and pass.

For 128 rows at O2, four lambda visitor types have 11852 B text / 7296 B rodata;
four sites sharing one named type have 5952 / 5760 B. These are object sections,
not MCU cycles or a universal application-size estimate. Root `.su` frames are
also individual frames, not a whole-call-chain peak. New MCU consumer timing
and actual call-chain/stack qualification remain Stage 14.

The legacy and optional protocol H7S receipts were refreshed after the name
fix: 3328 and 4300 checks per O2/Os image, with verified full 64 KiB restoration.
The current-code receipt again covers 166 library C++/qmake inputs. Both receipts
retain truthful prepublication HEAD/dirty markers and captured image hashes.

## Stage 15: software contract candidate

The [freeze suite](freeze/README.md) locks current public API return types,
mixed exact native values, traversal/As access and unsupported declarations.
It fixes wire 3.0 headers/codes/capabilities, LE/FNV constants, ABI revision,
the 32-B default storage budget, all eleven default resource ceilings,
dependency header/license blob pins and four independent
descriptor/values goldens. Each host O2/Os/Og run executes 29 counted
conditions; each runner invocation also requires 11 declaration rejections
and 11 separate ceiling mutation rejections.
ARM executes zero conditions and inspects linked symbols, sections,
disassembly and individual frames. Existing semantic/codegen suites remain.

The [Stage 15 decision](../../doc/StructuredTelemetryV3FreezeQualification.md)
maps the DoD to its owning evidence and records lifetime/concurrency limits.
Clean Qt 6.10.1 / MinGW 13.1 consumers passed: qualification's 97 checks,
the exact 41-byte client response and all four resource selections.
API/wire are a frozen contract candidate; full Stage 15 stays open on the
deferred Stage 14 execution of new probes on the original H7S. Neither host
checks nor archived board receipts substitute for that requirement.
