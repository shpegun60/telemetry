# Optional structured protocol example (original Stage 11)

The API correction of 2026-10-03 moved the implementation to
`examples/structured_protocol` and namespace `example::structured_protocol`.
This suite still exercises the same packet contract. Telemetry/resource core
and their `.pri` files do not select or require this protocol. Packet-only
statuses are owned by the example; endpoint statuses remain in telemetry.
The hardware figures/receipt below were refreshed on 2026-10-03 against
the protocol-relocation snapshot. They predate the final `telemetry` namespace
and `resource/telemetry/v3` path migration; they are historical measurements.
Current C++ declarations use `<telemetry/Telemetry.hpp>` and namespace
`telemetry`. The original receipt is preserved in the
[Stage 11 archive](../../../doc/evidence/pre-unification/stage11/receipt.json).

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](../../../lib/resource/LICENSE).

This slice adds transport-owned agreement and complete request dispatch over
the existing encoded indexes. The model, endpoint ABI, descriptor and values
formats are unchanged. The 24-byte data envelopes do not carry a fingerprint
or a session ID. Public integration contracts are in the
[example README](../../../examples/structured_protocol/README.md).

## Reproduce

```sh
python3 tests/structured/exchange/run.py --cxx g++ --build-dir build/exchange
python3 tests/structured/exchange/run.py --cxx g++ --null-checks --build-dir build/exchange-null
python3 tests/structured/exchange/run.py --cxx clang++-18 --sanitize --build-dir build/exchange-san
python3 tests/structured/exchange/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/exchange-arm
```

`--abi-only` reruns just the compiled-boundary controls. Each executed command
has its own log. The runner counts successful commands and expected rejections;
these are not counts of individual C++ assertions. CI runs the full default
runner, including host/sanitized/null-check jobs, ARM and retained board evidence.
The core-only/v3 qmake provider test excludes example sources. The separate
`qmake.pro` selects the example twice to check idempotent inclusion and calls
both compiled handlers; an omitted source therefore fails to link.

## Coverage

- `Check.cpp`: 4788 checks at budgets 16/32/64 and 4787 at budget 0 (the all-local
  Workspace-alias case is inapplicable there). Request lengths, response
  capacities and unaligned input positions; header mutations; all named
  WriteResult/CommandResult/ServiceStatus mappings; unknown callback status;
  target absence versus application Unavailable; ReadOnly precedence; full
  and partial input/output overlap; insufficient scratch and 4 KiB responses;
  active Workspace leases; header-only Workspace overlap; requestId zero/wrap
  and repeated execution. Native application values are encoded by `ts::encode`.
- `oracle.py`: freezes 74 independently specified Bind/request/response bytes
  using Python `struct`, without importing production encoding helpers.
- `Transport.cpp` / `FakeTransport.hpp`: a bounded two-peer integration example.
  Descriptor and values use the existing resource protocol. Ready gates values
  and controls; Unbound counts toward capacity. Reset clears old queues, does
  not recycle a busy context and excludes frames from old connections. The
  client's pending capacity and correlation matching preserve outstanding IDs
  across wrap. A real reordered model refuses the old descriptor fingerprint.
  Only Bind increments the agreement counter across the ordinary request loop.
- `NoHeap.cpp`: overrides global C++ allocation and runs Bind plus all three
  operations, including a 4 KiB Service response.
- `Negative.cpp`: ten rejected lifetime/copy/move forms. A reference-returning
  helper can still hide a dangling view; that remains a borrowed API precondition.
- `Arm.cpp`: retains both compiled boundaries and large/small handlers. GC and
  LTO link once with matching configuration and reject an independent mismatch
  in either Bind or Exchange. A volatile operation selector keeps both calls
  reachable; the first test draft accidentally let host optimizers delete Bind.
  The test was corrected, rather than loosening the link diagnostic.

The Stage 10 runner covers resource headers and its complete values/oracle
matrix remains enabled. This example suite compiles its own standalone
headers with protocol sources selected explicitly. Existing native endpoint
instruction-equality tests are unchanged.

Local validation uses MinGW GCC 13.1, WSL GCC 13.3 with null checks, Clang 18
ASan/UBSan, ARM GCC 13.2.1 and CubeIDE ARM GCC 14.3.1. The negative controls
include four ABI failures (Bind/Exchange, each with GC/LTO), in addition to ten
compile-time rejections. No malformed-request test requires heap in the library.
The host matrix uses dynamic test containers; the MCU fixture uses fixed arrays.

## ARM evidence

Linked `Arm.cpp` fixture, including its native callbacks and model. The
CubeIDE row is from the protocol API correction; the ARM GCC row is the
original Stage 11 measurement. Both are historical pre-migration figures:

| Compiler | O2 .text | Os .text | Og .text |
| --- | ---: | ---: | ---: |
| CubeIDE GCC 14.3.1 | 3620 B | 3432 B | 7128 B |
| ARM GCC 13.2.1 | 3596 B | 3672 B | 7276 B |

Exchange's own frame is 112/112/136 B at O2/Os/Og on both compilers.
CubeIDE Bind's frame is 48/32/32 B. Gates are 112 B for release and 144 B for
Og, covering every retained fixture frame; these limits are not measurements
of full call-chain peaks. The later [Stage 14 MCU qualification](../mcu/h7s/README.md)
records the core endpoint call-chain measurements separately.
The fixture's 12 KiB BSS is static request/response/scratch storage, not hidden
library allocation. No startup constructors, allocation, formatting or Scalar
conversion symbols are retained. Exchange's object has no Bind/descriptor/hash
dependency; its only peer state is the bound ModelView pointer.

## Historical H7S measurements, protocol-relocation snapshot

Actual NUCLEO-H7S3L8, Cortex-M7 600 MHz, I/D cache enabled, CubeIDE GCC 14.3.1,
default local budget 32 B. Both images passed **4300 checks**. The runner saved
and restored the full 64 KiB internal Flash and verified the read-back hash:
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.

Median cycles per operation, five warmed windows; interrupts masked only
during each timed window:

| Layer / operation | O2 | Os |
| --- | ---: | ---: |
| Native encoded index: Field write, 5-byte Config | 40.011 | 88.014 |
| Full Exchange: same Field write | 229.009 | 368.014 |
| Native encoded index: Command, 5-byte Config | 38.011 | 87.018 |
| Full Exchange: same Command | 235.009 | 380.014 |
| Native encoded index: Service echo, 5-byte Config | 44.010 | 128.014 |
| Full Exchange: same Service | 250.009 | 437.014 |
| Full Exchange: 4 KiB Service response | 12292.109 | 11960.219 |
| Bind: version/fingerprint agreement | 148.011 | 172.014 |

Exchange timings include envelope validation, Ready, index access, checked
native dispatch, status mapping and response header generation. The local
index timings do not. These are repeated calls in one small model, not the
Stage 08 random-RAM1024 experiment, and cannot be used as a like-for-like
old/new library speed ranking. The moved example has different generated
code placement; these fresh values do not establish cycle equivalence to
its original image. Callback and envelope checksums are verified;
the loop and checksum overhead are not subtracted.

The recorded [historical receipt](../../../doc/evidence/pre-unification/stage11/receipt.json)
identifies the exact captured code and
objects. `source_head=cd8b636bc8a518fcc1a3021659f109281d47107e` and
`source_dirty=true` identify the local protocol-relocation snapshot; completion
was `2026-10-03T11:45:47.235580+00:00`. Captured hashes identify its complete
contents. These fields are not relabelled with the final migration SHA. The
receipt verifier checks coverage/provenance
structure and 16 mutations; it is an offline check, not a fresh board run.

```powershell
python tests/structured/exchange/h7s/run.py --cube <copied-Cube-scaffold> `
  --arm-cxx <CubeIDE-arm-none-eabi-g++.exe> --programmer <STM32_Programmer_CLI.exe> `
  --serial <selected-adapter-serial> --port <selected-UART-port> `
  --output <new-build-directory> --run
python tests/structured/exchange/h7s/verify.py --self-test
```

Without `--run`, only images are built. Device execution requires explicit
`--programmer`, `--serial` and `--port`; there is no automatic adapter selection.
The historical run used ST-LINK `002A001F3033510135393935` / COM6. The live
runner verifies NUCLEO-H7S3L8 before programming, builds and authenticates both
images first, and restores Flash in `finally`. It does not alter option bytes
or external Flash. The former 3328-check scalar board suite is retired; the
[current structured MCU qualification](../mcu/h7s/README.md) supplies separate
Mixed/Scale evidence. Neither that evidence nor offline receipt verification
is a new execution of this optional protocol bench.

## Final-source H7S verification, 2026-10-03

The [current protocol evidence](h7s/README.md) and
[receipt](h7s/receipt.json) record the unified source tree at
`source_head=01fd180ff3678d7c47066f8a728dc1e02f0f3973` with
`source_dirty=true`. Both O2/Os images passed 4300 conditions and every timing
window; all 65536 internal Flash bytes were restored and verified. Captured
input/image hashes distinguish this run from the archived relocation build.
The earlier cycle table retains its original identity. The
[MCU qualification](../mcu/h7s/RESULTS.md) covers core Mixed/Scale operations
and observed call-chain stack separately. Offline validators check retained
evidence without operating hardware.

Remaining scope is deliberate: no real UART/TCP transport implementation,
authentication, client UI, retries or deduplication is added by this slice.
The [client example](../client/README.md) can use its payload codec without
selecting this protocol. Integrated qualification and MCU measurements remain
separate suites. Concurrent model owners still need application synchronization.
