# Bounded H7S owning/borrowed result comparison

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This separate fixture compares the new const-reference result paths with the
existing owning paths on NUCLEO-H7S3L8. No board execution is established by
creating these files or by compiling the images. Existing MCU receipts and
timing records remain separate.

The 14 operations cover Field and Service, each with owning native, borrowed
native, owning encoded and borrowed encoded paths for 4 KiB. For 64 KiB each
category has borrowed native, owning encoded and borrowed encoded paths.
An owning native 64 KiB result is never instantiated on the 16 KiB PSP.
Owning callbacks are opaque/noinline and the build uses no LTO. The same
opaque full-payload FNV consumer reads every byte in each measured path.
Results therefore include the consumer, function-pointer call and loop cost;
there is no baseline subtraction.

Two application-owned cached responses of each size, a 64 KiB output buffer,
and 65601 bytes of owning encoded scratch use static cacheable AXI RAM.
Their combined payload storage is 270401 bytes, within the scaffold's
466944-byte AXI region. A separate DTCM section contains a 16384-byte PSP,
256-byte guard and 512-byte selector sequence. ELF allocated section extents
must fit internal memory banks; all loadable LMAs must fit the complete
65536-byte internal Flash backup before `--run` can access a device.

Correctness runs outside timing/PSP samples. It checks exactly 127 conditions
and compares all 626688 encoded payload bytes across both cached responses.
It also checks pointer identity, owning/borrowed scratch requirements,
unavailable slots, owning callback failure, borrowed failure statuses and
owning response RAII. An explicit `BorrowedServiceResult<T>` Service callback
has additional native/encoded success and Busy checks for both sizes.
ARM native `BorrowedServiceResult` is 8 bytes for both
sizes; the owning 4 KiB response result is 4097 bytes.

Each operation has three explicit profiles: the same cached response,
alternating responses, and an independently reproducible shuffled selection
of those two responses. Each profile has seven DWT windows after cache
clean/invalidation and 128 warmup calls. Windows contain 64 calls for 4 KiB
or eight calls for 64 KiB. Observed per-call DWT deltas must be at most
100000000 cycles; a 64-bit accumulator rejects a window above 1000000000
cycles before another call. The DWT reads and bounds checks remain included
in reported cycles per call. IRQ masking is restored before UART output.

Every operation/profile also has three repeats with each of two stack fill
patterns. The reused [PSP trampoline](../../mcu/h7s/StackCall.S) restores
CONTROL and PSP; a volatile 512-byte control must visibly write at least
512 bytes. Each image emits 294 timing rows and 258 stack rows, including
six control rows. Watermarks report observed stack writes through nested
calls. They do not establish untouched reservations or a universal maximum.
Individual compiler frames are retained separately as `.su` artifacts.

Build only, using a fresh output directory and the copied scaffold:

```powershell
$armCompiler = (Get-ChildItem 'C:/ST/*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3*/tools/bin/arm-none-eabi-g++.exe' | Select-Object -First 1).FullName
python tests/structured/borrowed/h7s/run.py --cube build/stage13/h7s-protocol/scaffold --arm-cxx $armCompiler --output build/borrowed/h7s-offline
python tests/structured/borrowed/h7s/verify.py --receipt build/borrowed/h7s-offline/receipt.json --build-only --artifacts build/borrowed/h7s-offline --self-test
```

Build mode reuses [h7s_support](../../../h7s_support/README.md), captures the
library plus the concrete fixture/header/runner/verifier/trampoline inputs,
and copies/hashes the Cube scaffold. It records factual source HEAD/dirty
state, compiler identity, flags, image/object/linker hashes, memory extents
and assembly. It imports no serial package, enumerates no adapters and
invokes no programmer. Its receipt remains `completed=false`.

Only the designated device operator runs the following explicit command;
it uses another fresh directory and builds both images before programming:

```powershell
python tests/structured/borrowed/h7s/run.py --cube build/stage13/h7s-protocol/scaffold --arm-cxx $armCompiler --output build/borrowed/h7s-live --run --serial 002A001F3033510135393935 --port COM6 --programmer C:/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe
python tests/structured/borrowed/h7s/verify.py --receipt build/borrowed/h7s-live/receipt.json --artifacts build/borrowed/h7s-live --self-test
```

Before programming, the runner confirms the selected serial, board and
device ID `0x485` and retains exactly 65536 Flash bytes. Every programmed
image is verified. Its `finally` path restores the complete backup, uploads
65536 bytes again, compares hashes and resets the restored image to run.
This path also applies to UART/parser failures. A terminated host process
or loss of power requires restoration from the retained `before.bin`.

The strict offline verifier requires exact setup/counts, operation order,
complete unique timing/stack coverage, independent checksums, callback
counts, bounded cycles, static RAM spans and full restoration. Its parser
also requires exact scaffold/common-object/stack-usage sets, authenticates
every `.su` file and section log, checks actual binary length and recomputes
Flash/RAM spans from both authenticated logs and the retained ELF using
the compiler's companion objdump. Artifact self-tests reject removed entries,
changed hashes/lengths and a forged one-byte Flash span against real files.
Its UART
controls can also run alone with `verify.py --self-test`; synthetic control
rows are explicitly parser checks and are never a measurement receipt.

The actual O2/Os device run at sealed code `7b73fb4` is retained in
[receipt.json](receipt.json); [RESULTS.md](RESULTS.md) reports its measured
cycles and observed PSP, including the scope and limits of the comparison.
Creating or rebuilding this fixture does not reproduce that device execution.
