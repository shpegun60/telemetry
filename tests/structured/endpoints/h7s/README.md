# Stage 08 H7S correctness and timing

Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.

Measured on 2026-09-27: NUCLEO-H7S3L8, Cortex-M7 at 600 MHz, 32 KiB
D-cache, CubeIDE ARM GCC 14.3.1, hard float, no LTO. These are fixture
measurements, not whole-firmware latency or H753 results.

This page retains the earlier **TypeKind leaf-only** and entry-alignment
experiments. The later user-approved object-size budget replaces that storage
policy. Current 0/16/32/64 results, stack frames and remaining dispatch costs
are in [STORAGE_RESULTS.md](STORAGE_RESULTS.md). The following dispatch ABI
optimization is in [DISPATCH_RESULTS.md](DISPATCH_RESULTS.md). Old receipts are historical;
they do not claim execution of the later implementation.

## Reproduce

The runner copies the COBS Cube scaffold, builds all images first, saves
the full 64 KiB internal Flash and restores it in `finally`. Board identity,
programming verification and equal backup/read-back SHA-256 are mandatory.
It does not program option bytes or external Flash. Backups/binaries remain
in the output directory. An interrupted host or power loss still requires
restoration from the retained `before.bin`.

```powershell
$armCompiler = (Get-ChildItem 'C:/ST/*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32*/tools/bin/arm-none-eabi-g++.exe' | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$programmer = (Get-ChildItem 'C:/ST/*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer*/tools/bin/STM32_Programmer_CLI.exe' | Sort-Object FullName -Descending | Select-Object -First 1).FullName
python tests/structured/endpoints/h7s/run.py --cube path/to/h7s_cobs_test --arm-cxx $armCompiler --programmer $programmer --output build/structured-new-run --legacy --components --run
```

Select the actual `--serial` and `--port` if different from the recorded
ST-LINK / COM6. Omit `--run` for compilation only; `--variants Natural`
selects the production layout. Existing output directories are refused.

## Measurement contract

Each image runs the mixed Field/Command/Service and 4 KiB Field/Command
correctness fixtures first. Runtime probes occupy a separate TU without
LTO. Flash has 64 entries sharing one definition per family. RAM has
64 or 1024 entries pointing to independent definition objects. Every array
base is aligned to 32; only element alignment/stride varies.
That describes the earlier definition-based snapshots on this page. The
current Context variant stores direct owner pointers instead, as documented
in the dispatch report; both old and new paths still call the same Device.

Profiles are repeated ID, sequential IDs and a fixed shuffle, precomputed
in DTCM. Each window cleans/invalidates caches, warms 2048 calls and times
16384 calls with interrupts masked. UART runs after restoring the mask.
Five windows per case are retained. Medians include the loop, indirect probe
and result consumption; no baseline is subtracted. Host validation checks
checksums, case coverage, repetitions and duplicate/missing windows.

`--legacy` adds unchanged RW32 scalar endpoints in the same image, bound
to the same Device. Both paths consume the same mutable four-byte LE input.
The scalar adapter encodes `read<uint32_t>()`, decodes before `write(id,u32)`
and creates one Scalar for command execution. Both read probes materialize
output bytes before the checksum.

The new command takes **`const Request&`**, with one u32 member; only the
legacy command takes a bare u32. This compares the cost of performing that
operation under different contracts: the new command constructs a real
request in caller storage and checks its encoded boundary. There is no
legacy aggregate/array or Service equivalent.

## Component profiling and retained changes

Standalone component costs are not additive: full adapters have different
inlining, register allocation and code placement. Before helper optimization
at O2, including loop/probe overhead:

| Probe | Cycles/call |
|---|---:|
| Local u32 load/store with compiler barrier | 19.00 |
| Reserve/construct/read/release u32 | 35.00 |
| Reserve/construct/read/release Request | 35.00 |
| Canonical u32 byte decode | 39.00 |
| Reserve + Request decode + release | 55.00 |
| Lookup + read wire size | 30.00 |

At Os reserve-Request took 64 cycles versus 39 for u32: the Request
constructor was outlined. Disassembly also showed repeated span-check
calls and a four-iteration byte-decode loop. Retained changes:

1. Endpoint preflight checks spans once; internal codec steps reuse that
   proof. Standalone Codec retains its complete validation.
2. Integer leaves use `memcpy` on little-endian targets, byte assembly
   otherwise. No unaligned U& or raw aggregate copy is formed. Target flags
   govern generated unaligned instructions. Decode fell from 39 to about
   16 cycles in the isolated probe.
3. Inline reservation/overlap helpers retain fresh-lease facts at Os.
   Capacity, alignment, construction and RAII remain checked.
4. Scalar/Enum Field leaves use locals selected by **TypeKind**. Array/Struct
   always use Workspace, even at one byte. A `sizeof(T) <= 8` experiment
   was rejected; it is not the production policy.

A status-only erased read result experiment reduced image size but did not
improve O2 and regressed Os read timing. It was reverted. Service binding
semantics and the legacy scalar library were not redesigned.

## Final natural layout versus legacy

RAM1024 shuffled, same final image, cycles/call:

| Operation | Old O2 | New O2 | Old Os | New Os |
|---|---:|---:|---:|---:|
| Read u32 to four bytes | 60.05 | 85.30 | 86.05 | 70.21 |
| Write u32 from four bytes | 42.04 | 83.14 | 46.04 | 80.91 |
| Command setting one u32 | 42.01 | 81.09 | 46.00 | 106.22 |

The initial new implementation used 99.16/131.11/144.10 cycles at O2 and
210.26/298.42/318.24 at Os. Those were earlier images; use the table above
for old/new ratios. Linked function placement also moves unchanged legacy
timings. Do not subtract unrelated images as isolated component costs.

The encoded path improved substantially, but **does not universally
outperform the scalar-specific legacy adapter**. Writes and Command remain
slower here. No maximum-speed claim is made. Native controls tie: read 8/8,
write 9/9, command 9/9 at O2; 8/8, 11/11, 11/11 at Os. ARM gates independently
compare exact direct/local/global bytes for eight Field shapes plus u32
read/write and Command.

## Final entry-alignment experiment

Field/Command/Service entries are 24/20/24 B, alignment 4 on ARM. Align 8
produces 24/24/24 B; align 32 produces 32/32/32 B. Definitions, names and
Registry metadata are additional storage.

RAM1024 shuffled, final optimized code, cycles/call:

| Operation | Natural O2 | Align 8 O2 | Align 32 O2 | Natural Os | Align 8 Os | Align 32 Os |
|---|---:|---:|---:|---:|---:|---:|
| Read | 85.30 | 85.19 | 87.92 | 70.21 | 70.23 | 73.89 |
| Write | 83.14 | 83.03 | 88.25 | 80.91 | 80.88 | 84.67 |
| Command | 81.09 | 83.73 | 86.94 | 106.22 | 108.73 | 112.89 |
| Service | 124.60 | 124.60 | 128.90 | 131.79 | 131.76 | 132.51 |

Natural alignment remains the production choice. Align 32 does not improve
this matrix. It also moves code and changes indexing instructions; these
numbers do not isolate D-cache effects from code placement. Legacy Field
remains RW32. Natural test images occupy 47208 B at O2 and 47608 B at Os,
including HAL, probes and correctness checks, not just library code.

## Retained evidence

- [legacy-receipt.json](legacy-receipt.json): initial old/new comparison,
  two images, 690 windows.
- [components-before-receipt.json](components-before-receipt.json): after
  duplicate-check removal, before helper/leaf changes; two images, 790 windows.
- [final-receipt.json](final-receipt.json): final code, three alignments,
  six images, 2370 windows.

All runs passed both MCU correctness fixtures and restored the saved Flash.
Receipts identify source snapshots and image hashes. Later documentation or
verifier edits do not imply another hardware execution. CI validates stored
evidence, not a connected board. Artifact verification recalculates image
and source hashes; it was run locally for all three receipts.

After measuring, compile-time guards were added for temporary tables passed
through explicit `group` types or braced Model arguments. A build-only repeat
produced identical complete natural Flash binaries at O2 and Os; see
[constructor-guard-equivalence.json](constructor-guard-equivalence.json).
This is binary-equivalence evidence for this fixture, not another board run.

```sh
python tests/structured/endpoints/h7s/verify.py --receipt tests/structured/endpoints/h7s/final-receipt.json --self-test
# Add --artifacts path/to/original/output to recalculate image and source hashes.
```

Sixteen altered-receipt cases check image/window coverage, checksums,
correctness failures, identity and restoration. Offline verification alone
makes no claim that current code ran on hardware.
