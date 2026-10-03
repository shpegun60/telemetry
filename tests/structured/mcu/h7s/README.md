# Stage 14 H7S execution and measurements

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

The actual 2026-10-03 run passed all four images and restored internal Flash.
See [measured results](RESULTS.md) and [retained receipt](receipt.json).

This separate runner builds and, only with `--run`, executes the existing
[MCU probe bodies](../README.md) on a NUCLEO-H7S3L8. The adjacent
[`mcu/run.py`](../run.py) remains an offline host/ARM runner. Build mode imports
no UART package, enumerates no device and calls no programmer.

The fixture uses the copied Cube scaffold's USART3 at 115200 baud, internal
64 KiB Flash and 600 MHz clock. It retains the Device/no-access MPU region
at `0x25000000` before enabling caches. Cacheable AXI SRAM contains explicit
endpoint scratch/output storage; the sequence and independent measurement
stack occupy DTCM. The board's measured memory addresses and entry/view
layouts are reported. This is an H7S measurement; H753 timings are not
established by it.

Resolve the CubeIDE ARM 14.3.1 compiler locally and supply a copied scaffold
with `bench_init`/`bench_loop` hooks. One retained local scaffold is
`build/stage13/h7s-protocol/scaffold`. Its original project is never edited.

```powershell
$armCompiler = (Get-ChildItem 'C:/ST/*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3*/tools/bin/arm-none-eabi-g++.exe' | Select-Object -First 1).FullName
python tests/structured/mcu/h7s/run.py --cube build/stage13/h7s-protocol/scaffold --arm-cxx $armCompiler --output build/stage14/h7s-build
```

Every run requires a fresh output directory. All four images (Mixed/Scale,
`-O2`/`-Os`) must link and fit the internal Flash before any device operation.
The library, qualification sources, MCU bodies, assembly trampoline and
runner/verifier sources are captured with normalized LF SHA-256. The Cube
scaffold is separately copied and hashed. Each image retains its compiler
flags, linker flags, ELF/binary/object digests, assembly, map and `.su` frames.
The receipt records the factual source HEAD and full dirty state. A build
receipt has `completed=false` and `execution="compile/link only"`; it is not
an execution result.

For a hardware run, explicitly supply the selected adapter serial, UART
port and programmer executable. There are no device defaults or enumeration.

```powershell
python tests/structured/mcu/h7s/run.py --cube build/stage13/h7s-protocol/scaffold --arm-cxx $armCompiler --output build/stage14/h7s-live --run --serial SELECTED_SERIAL --port SELECTED_COM --programmer C:/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe
```

Before its first write, the runner uploads exactly 65536 bytes of internal
Flash, confirms the selected adapter serial, NUCLEO-H7S3L8 board and device
ID `0x485`, and records the backup digest. Every programmed image is verified.
The `finally` path restores the retained backup, uploads 65536 bytes again,
compares its SHA-256 and resets the restored image so it runs. This path also
runs after UART, correctness or parser failures. Option bytes and external
memory are untouched. A terminated host process or loss of power requires
manual restoration from the retained `before.bin`.

## Measurement contract

- The unchanged Mixed correctness body checks 12230 conditions, including
  the existing 97 multi-translation-unit consumer conditions. Scale checks
  2831 conditions. The MCU-only owning `readAs<Big>` comparison adds three
  separate conditions to Mixed: all 1024 words, agreement with encoded
  `big_read`, and the repeated group's return. These are reported separately.
- Mixed preserves direct owner, bound slot, native local/global, named visitor,
  native runtime `readAs`, encoded read/write/Command/Service, 4 KiB Field,
  4 KiB Command input, 4 KiB Service input/output, descriptor streaming,
  packed descriptor copy and Values reads. Scale preserves 128 targets and
  its direct/new/legacy scalar comparisons. No production endpoint is added.
- Full-byte response comparison, every descriptor cursor, large in-place
  Service overlap and insufficient-workspace refusal remain in the existing
  correctness bodies. The packed descriptor's full bytes, size, fingerprint
  and Values size are emitted outside timed/stack calls for independent
  protocol validation.
- Same-ID, sequential and deterministic shuffled profiles use the exact
  128-ID sequence from `mcu/Fixture.hpp`. Fixed-position roots run only the
  same-ID profile. Runtime roots run all three profiles. Seven DWT windows
  are recorded per supported operation/profile. Iteration counts come from
  the original operation definitions; the new `native_big` uses 128 calls.
- Each timing window masks interrupts, cleans/invalidates D-cache,
  invalidates I-cache, warms 128 calls, then times the full call loop. The
  common function-pointer call, sequence selection, loop and checksum cost
  remain included. No baseline is subtracted. IRQ masking is restored before
  formatting or UART I/O.
- Each operation/profile also runs on the established assembly PSP
  trampoline, using a 16 KiB DTCM stack above a 256-byte guard. Three repeats
  use each of two fill patterns (`0xa5`, `0x5a`). The callback executes the
  same loop and checks its result. CONTROL and PSP restoration and the guard
  are checked. A volatile 512-byte positive control must visibly write at
  least 512 bytes for both patterns.
- The reported watermark is **observed stack writes including nested calls**.
  It does not measure untouched reserved slots and is not a guaranteed
  worst-case stack bound. Compiler `.su` records show individual static
  frames separately. Native `readAs<Big>` deliberately owns its return on
  the stack and is compared with the existing caller-workspace encoded path.
- Raw UART lines are retained in each image's receipt and log. The verifier
  requires exact identities/counts, unique complete timing/stack coverage,
  independent checksums, positive bounded cycle counts, descriptor structure,
  stack geometry/control, artifact hashes and a verified full restoration.
  Timing and observed stack summaries are generated only after that passes.

Offline receipt verification uses the adjacent [`verify.py`](verify.py).
It does not open a port or invoke a programmer. Without the retained artifact
directory, a receipt can establish its internal consistency and captured
source identity; a physical board run cannot be authenticated from JSON alone.
