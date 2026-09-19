# Field layout: measured H7S result and production choice

Selected **B32**: reorder Getter/Setter/FieldType before names and IDs, then
align Field to 32 bytes. ARM32 Field is 96 bytes. Read/write algorithms,
numeric conversion, limits, enum handling and metadata contents stay unchanged.
There is no duplicated type/flag state and assignment remains available.
The positional constexpr constructor preserves existing table rows/defaults;
Field is no longer an aggregate, so designated initializers require migration.

## Recorded hardware run

NUCLEO-H7S3L8, Cortex-M7 at 600 MHz, I/D caches enabled, D-cache 32 KiB.
CubeIDE GNU 14.3.1, C++17, hard float, `-O2` / `-Os`, no LTO, exceptions or RTTI.
The complete method is in [README.md](README.md). Each number below is the
median of five 32768-call windows, including loop/call/result-consumption cost.
Eight images passed all **3680 windows / 120,586,240 measured calls** with the
expected checksums. Original 64 KiB firmware was restored and read back with
SHA-256 `a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.

## Large RAM table, shuffled IDs

1024 mixed fields in cacheable AXI SRAM, cycles per call; smaller is better.

| Optimization / operation | A: old 80 B | B: reordered 80 B | B32: aligned 96 B | C: duplicated state 96 B |
|---|---:|---:|---:|---:|
| O2 Scalar read | 104.20 | 91.90 | **83.87** | 82.88 |
| O2 float read | 98.48 | 86.77 | **78.98** | 78.24 |
| O2 float write | 94.82 | 91.56 | **90.01** | 94.88 |
| O2 U16 write | 83.38 | 80.79 | **77.38** | 83.47 |
| Os Scalar read | 106.43 | 94.86 | **86.99** | 91.12 |
| Os float read | 95.90 | 84.36 | **76.49** | 78.71 |
| Os float write | 102.44 | 99.45 | **98.04** | 100.80 |
| Os U16 write | 93.89 | 91.64 | **87.96** | 88.06 |

Against A, B32 reduces O2 cycles by 19.5% for Scalar read, 19.8% for float
read, 5.1% for float write and 7.2% for U16 write in this workload. Sequential
large-table reads show a similar improvement. C reads about one cycle faster
at O2, but loses that advantage at Os and has worse mixed writes. It is not
a consistent winner; B32 is the simpler choice with broad gains.

## Limits of the gain

For the 128-field Flash table, shuffled O2 Scalar reads are 84.63 / 83.59 /
83.59 / 82.59 cycles for A/B/B32/C; float reads are 77.91 for all four.
The extra alignment buys little when the accessed table lines already fit
in cache. Known F32 reads remain about 20 cycles including the harness loop
at O2, with identical 8-byte getter wrappers.

Lookup alone does not improve: the hot O2 fixture reports 25 cycles for A
and 26 for B/B32/C, despite equal 16-instruction lookup bodies in the object
comparison. Whole-image placement/scheduling can change small differences.
This is not a claim of a universal maximum or faster execution in every case.
The final firmware should still be profiled on its actual H753 memory map.

The 96-byte stride costs **16 bytes per field**, or 16 KiB for 1024 rows.
That is Flash for Flash-resident tables and RAM for RAM-resident tables. Mutable
source-owner storage is unchanged. The read members stay within one 32-byte
line for every array element; caches may still fetch index, source, callback,
instruction and limit data separately. Restricted writes can touch cold bounds.

## Evidence and correctness checks

[samples.csv](samples.csv) preserves every original timing window;
[receipt.json](receipt.json) records compiler/input/image identities, device
reports and restoration. B32 is bold as the selected variant, not as the
minimum in every row. Raw binaries, programmer logs and backups remain in
the ignored local `build/field_layout_experiment/h7s/live-1` directory.

Before the hardware run, A/B/C each passed 578 C++17 checks, 580 C++20 checks
and 35 expected compile failures; C additionally passed five immutability
rejections. Clang 18 C++17 ASan/UBSan/float-cast-overflow passed all three.
A32/B32 separately passed 578 GCC C++17 checks and 35 expected rejections.
Schema and value JSON matched across candidates. The production implementation
also retains the independent host/ARM storage and code-generation checks.
