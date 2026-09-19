# H7S telemetry Field benchmark

Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.

This harness uses a copied NUCLEO-H7S3L8 Cube scaffold from the COBS project.
The original project is not edited. Its `bench_init`/`bench_loop` hooks,
600 MHz clock setup, USART3 at 115200 and internal-flash linker script are
required. The runner builds all selected images before touching the board.

Copy `Boot/Core`, `Drivers` and `Boot/STM32H7S3L8HX_FLASH.ld` from
`COBS/stm32_cube_test/h7s_cobs_test` into an ignored local build directory.
Do not copy its `out` directory. Keep every original vendor license/header.
The recorded copy is `build/field_layout_experiment/h7s/scaffold`.

```powershell
$armCompiler = (Get-ChildItem 'C:/ST/*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32*/tools/bin/arm-none-eabi-g++.exe' | Select-Object -First 1).FullName
# Build only:
python tests/field_layout/h7s/run.py --cube build/field_layout_experiment/h7s/scaffold --arm-cxx $armCompiler --output build/field_layout_experiment/h7s/new-build
# Fresh build, real DWT measurements, and original-image restoration:
python tests/field_layout/h7s/run.py --cube build/field_layout_experiment/h7s/scaffold --arm-cxx $armCompiler --output build/field_layout_experiment/h7s/new-live --run --serial 002A001F3033510135393935 --port COM6
# Compare pinned B32 against the current production checkout (RW32):
python tests/field_layout/h7s/run.py --cube build/field_layout_experiment/h7s/scaffold --arm-cxx $armCompiler --output build/field_layout_experiment/h7s/new-current --variants B32 Current --run
```

Select the actual probe serial and COM port on another machine. `pyserial`
and STM32CubeProgrammer are needed only with `--run`. Existing output
directories are refused. Build products, the original image, read-back image,
UART reports, source/image hashes and all timing windows remain there.

Before any programming, the runner reads the current 64 KiB internal flash.
Every image must fit that region and is verified after programming. A `finally`
block restores the backup and compares a new 64 KiB read-back hash. Option
bytes and external memory are not programmed. An interrupted host process or
power loss still requires manual restoration from the retained `before.bin`.

## Measurement contract

- Four historical candidates A/B/B32/C, each at `-O2` and `-Os`; `Current`
  uses the exact production library. C++17, Cortex-M7 hard float, no LTO,
  exceptions or RTTI. Historical inputs are pinned to `688ae7e`.
- Internal Flash holds 128 constant fields; a placement-constructed 1024-field
  array occupies cacheable AXI SRAM. Its first 128 fields are a separate
  small-RAM control. Array bases are aligned to 32 bytes for every variant.
- Eight repeated field kinds match the compiler fixture. Setup validates
  every readable value and ID and both missing-ID boundaries before timing.
  Const definitions in C and Current are reconstructed only before publishing the views;
  the final pointer is laundered. No allocation occurs during measurement.
- Six ID profiles: repeated native F32, bounded F32, native U16, enum U16;
  sequential; deterministic shuffled IDs. A complete 1024-entry sequence is
  precomputed in DTCM so random-number generation and sequence cache traffic
  do not enter the comparison.
- Five runtime operations: lookup, Scalar read, float read, float write and
  U16 write. Two known-ID controls run on the first Flash field. The measured
  loops include call/loop/result-consumption cost; no baseline is subtracted.
- Each window cleans/invalidates D-cache, invalidates I-cache, warms 2048
  calls, then times 32768 calls. Interrupts are masked during warmup/timing,
  and restored immediately afterwards. UART formatting/transmission is outside
  the window. There are five repeats, 460 windows per image.
- The host requires the exact device/layout/clock report, complete coverage,
  unique window keys, bounded positive cycle counts and independently computed
  result checksums. A failure is retained and still triggers restoration.

This measures the H7S3's M7 and its 32 KiB D-cache, not H753 memory latency.
Small hot tables, large RAM tables and constant-ID access remain separate in
the [RW32 results](RW32_RESULTS.md) and [historical B32 results](RESULTS.md).
Firmware-wide latency, interrupt response and cold
single-read latency are not established by these masked steady-state windows.

The retained evidence can be checked offline, including deliberately damaged
copies of the records. These commands do not access the board:

```sh
python tests/field_layout/h7s/verify.py --self-test
python tests/field_layout/h7s/verify.py --receipt tests/field_layout/h7s/final-receipt.json --samples tests/field_layout/h7s/final-samples.csv --self-test
python tests/field_layout/h7s/verify.py --receipt tests/field_layout/h7s/rw32-receipt.json --samples tests/field_layout/h7s/rw32-samples.csv --self-test
```

Each image now records SHA-256 of `Probe.o` and `Benchmark.o`. The two historical
receipts were supplemented from the original local objects, after checking
their original ELF and binary hashes; `object_hash_provenance` records this
later step explicitly. Timing samples and original image hashes were not
changed. The final receipt also declares B32/Current Probe equivalence for
O2/Os, which the verifier checks against those object hashes. In that historical
receipt, Current still meant B32. In `rw32-receipt.json`, Current is RW32;
the implementations differ and no Probe-equivalence claim is made.

Add `--artifacts build/rw32-live-inline` to the RW32 command to check its
retained binaries and objects. RW32 source/object hashes were recorded during
the run, with the normalized export described in the receipt.

Add `--artifacts build/field_layout_experiment/h7s/final-live` to the final
verification command to check the actual retained object, ELF and binary bytes.
Without that directory the verifier checks recorded identities and consistency;
it cannot authenticate a physical board run. Raw binaries and programmer logs
remain local. To supplement another old receipt into a **new** file:

```sh
python tests/field_layout/h7s/verify.py --receipt old-receipt.json --samples old-samples.csv --artifacts path/to/original-run --record-objects new-receipt.json --compare-probes B32 Current
```

Only request `--compare-probes` when both variants really have identical Probe
objects; unequal or missing objects fail verification. Recording refuses an
existing output file and refuses mismatches with previously recorded hashes.
