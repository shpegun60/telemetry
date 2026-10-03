# Stage 10: structured resource providers

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

`DescriptorFile` exposes either immutable packed bytes or indexed streaming.
`ValuesFile` uses the existing typed Field read thunks and caller-owned
Workspace. Its header is 24 bytes: `TVL3`, major/minor, field count, file size
and the cached u64 descriptor fingerprint. This last field was added at the
user's request during Stage 10; it identifies a schema, not live payload
integrity. The active providers live in `resource/telemetry/v3` and namespace
`resource::telemetry::v3`; their native endpoints use `<telemetry/Telemetry.hpp>`
and namespace `telemetry`. Descriptor v3.0 bytes remain unchanged. Legacy
v2.1 providers and their decoder are retired.

## Reproduction

From the repository root:

```sh
python3 tests/structured/resources/run.py --cxx g++ --build-dir build/resource-v3
python3 tests/structured/resources/run.py --cxx g++ --null-checks --build-dir build/resource-v3-null
python3 tests/structured/resources/run.py --cxx clang++-18 --sanitize --build-dir build/resource-v3-sanitized
python3 tests/structured/resources/run.py --cxx arm-none-eabi-g++ --arm --build-dir build/resource-v3-arm
python3 tests/structured/resources/run_qmake.py --qmake qmake6 --build-dir build/resource-qmake
python3 tests/structured/resources/h7s/verify.py --self-test
```

On Windows select the CubeIDE ARM compiler, not CubeCLT; use the Qt MinGW
compiler/runtime together. For qmake pass `--make mingw32-make` and the
installed Qt kit's qmake path. Generated objects/logs go to the selected build
directory. CI runs this suite in C++20 host, sanitizer, null-check and ARM
jobs; the Qt job exercises core-only and telemetry-v3 selections.

## What the checks establish

- `Check.cpp`: 5700 exact cursor/capacity combinations per storage budget
  0/16/32/64; all legal header/token/EOF positions and every payload interior;
  canaries and getter counts; 81 whole-file chunk reconstructions;
  empty types/catalogs, slot bind/reset, copies and fixed `size()`;
  packed/streamed descriptor parity at every starting offset; ordinary
  resource READ/STAT and read-only behavior.
- Workspace checks: whole token must fit before scratch/getter use; all four
  u32 alignments; 4 KiB return constructed in caller storage; insufficient
  space and an occupied caller lease; output overlap rejection without
  mutation; all-local reads may use output which aliases their unused scratch.
- `Edges.cpp`: exact U64/S64/NaN bits, an unknown representable enum code in
  the main fixture, rejected duplicate-name metadata, and a 65537-byte token
  that exceeds protocol v1's 65535-byte payload. The latter returns a header
  prefix followed by explicit BufferTooSmall, without invoking the getter;
  a sufficiently large direct READ succeeds.
- `values.hex` freezes 73 wire bytes. `check_values.py` independently builds
  them, parses the producer descriptor through the Stage 09 oracle, and
  checks header identity/count/length, whole tokens, status and zero absent
  payload. Its 121 cases include a mismatched descriptor fingerprint and
  truncated/malformed files. This is test infrastructure, not Stage 12 UI.
- Twelve rejected compilations cover temporary/braced descriptor storage and
  temporary/const/proxy Workspace, including explicit provider types.
  Two real-call ABI mismatch links cover section GC and LTO. The matching
  builds succeed; a reader compiled with a different storage policy cannot
  link. Negative checks require the relevant constructor or reader symbol.
- `NoHeap.cpp` rejects C++ allocations during runtime construction/reads.
  ARM linked-symbol checks reject heap allocation, decimal formatting and
  retained legacy Scalar conversion. Constant providers have no init-array
  section. ARM stack gates are 104 B at O2/Os and 128 B at Og per emitted frame,
  covering the reader, wrappers and 4 KiB Field thunk; these are not a total
  call-chain proof or a bound on an application's arbitrary getter.
- All optional `resource/telemetry/v3` headers compile alone with their own
  dependencies. The generic resource header suite stays independent of PFR.
  The original Stage 09 `c25c6fa` CI integration failure was caused by a
  recursive legacy sweep selecting those headers without PFR include paths;
  separate header coverage avoids that dependency in resource-only builds.
- `resources.pro` builds and runs core-only and v3. `.pri` dependencies are
  explicit; repeated telemetry/resource includes do not duplicate sources.
  The application facade exposes only `FileSystemView`. The compiled v3
  reader remains `resource/telemetry/v3/detail/Values.cpp`; resource-only
  builds do not select telemetry, PFR or the optional protocol example.

The first Stage 10 CI run on `a3eb7dc` exposed a stale **legacy** board receipt:
the optional qmake changes made its recorded `resource.pri` hash differ. A
historical legacy H7S run published in `2b66bef` passed all 3328 checks at
O2/Os and refreshed that snapshot's input evidence without loosening the
verifier. That scalar suite is no longer an active final-API runner. This is
separate from the Stage 10 measurements below;
neither the provider implementation nor its wire bytes changed for that fix.

## Historical local results, 2026-09-27

These figures describe the original Stage 10 images, not a fresh measurement
after the final namespace/path migration. The active runners retain their
correctness, wire, header, symbol and frame checks.

MinGW GCC 13.1, Linux GCC 13.3 with null checks, and Clang 18 with ASan/UBSan
execute all host cases. Host runners report 27 passing commands, 14 intended
rejections and 121 independent wire checks. ARM GCC 13.2.1 (CI compiler) and
CubeIDE GCC 14.3.1 pass 41 commands plus 14 rejections at O2/Os/Og. Qt 6.10.1
MinGW originally built/ran all four then-supported selections. The final
runner selects only generic core or v3. The legacy resource results belong
to the historical snapshot; Stage 09's active 22 rejected declarations and
1729 parser checks remain independent coverage.

The linked inspection fixture retains mixed small values, packed descriptor
READ and a 4 KiB Field READ. Its RAM includes the caller's 4099-byte scratch,
slots and counters; no hidden 4 KiB stack object is introduced.

| Toolchain / optimization | .text | .rodata | .data | .bss | Reader frame | Wrapper frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CubeIDE 14.3.1 O2 | 2152 | 1800 | 16 | 4168 | 96 | 48 |
| CubeIDE 14.3.1 Os | 1940 | 1776 | 16 | 4168 | 88 | 48 |
| CubeIDE 14.3.1 Og | 3216 | 1816 | 16 | 4168 | 120 | 64 |
| ARM GCC 13.2.1 O2 | 2176 | 1800 | 16 | 4168 | 88 | 48 |
| ARM GCC 13.2.1 Os | 2064 | 1776 | 16 | 4168 | 88 | 48 |
| ARM GCC 13.2.1 Og | 3240 | 1816 | 16 | 4168 | 120 | 64 |

Keeping common provider metadata ready in the object, instead of constructing
a temporary view for each READ, reduced the CubeIDE linked fixture's .text by
88/108 B at O2/Os. Its wrapper frame fell from 80 to 48 B; the reader frame
grew by 8 B. No behavior/storage-policy change accompanied that adjustment.

## Historical NUCLEO-H7S3L8 measurement

The [receipt](h7s/receipt.json) records
`source_head=c25c6fa00f91a7b6baac9ac23299c2ff29396f62`, CubeIDE GCC 14.3.1,
a 600 MHz Cortex-M7,
caches enabled, both built images and their library/object digests. The board
ran all 5700 cursor/capacity cases, checked bytes against the frozen fixture,
getter counts, a too-small large-token buffer and an actual 4 KiB read.

Median DWT cycles per provider read, five windows per case:

| Operation | O2 | Os |
| --- | ---: | ---: |
| Whole 73-byte file, 10 Fields including two absent slots | 963.01 | 957.01 |
| Resume one 8-byte Point token | 202.01 | 230.01 |
| Whole 4126-byte file, u32 + 4 KiB array | 14724.20 | 13184.11 |

The first two use 4096 calls per window, the large file 256. Each reported
window has an independently checked output checksum. These measurements are
for the provider read, not UART framing/transmission or comparison with the
old scalar library. The small file includes header construction and two empty
slot checks; it is not ten direct native getter calls.

Only the first 64 KiB of internal Flash was temporarily replaced. Both images
were built before any write. Restore/read-back matched the original SHA-256:
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
No option bytes or external Flash were changed. The run completed at
2026-09-27 17:09:08 UTC. The receipt predates `source_dirty` recording and
the final namespace migration; its captured hashes identify the local measured
build. `verify.py` checks receipt consistency and sixteen mutations; it does
not represent a new run of later code.
Before publication, all 164 captured C/C++ library and `.pri` files were
compared with the working sources for both images; their SHA-256 values
matched. Documentation was subsequently updated with these measurements.

```powershell
python tests/structured/resources/h7s/run.py `
  --cube C:/path/to/h7s_cobs_test `
  --arm-cxx C:/path/to/CubeIDE/arm-none-eabi-g++.exe `
  --programmer C:/path/to/STM32_Programmer_CLI.exe `
  --serial 002A001F3033510135393935 --port COM6 `
  --output build/resource-v3-h7s-new --run
```

The output directory must not exist. With `--run`, explicit `--programmer`,
`--serial` and `--port` are required; they have no device defaults. Without
`--run`, the runner only builds both images. The helper verifies the expected board,
backs up Flash, runs the checks, and restores/verifies the backup in `finally`.
