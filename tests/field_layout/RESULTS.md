# Field layout experiment: CubeIDE GCC 14.3.1

The local A/B/C experiment does not establish a universal performance winner.
B improves proximity of read metadata without growing Field. C guarantees
that its read and unrestricted-write Field members fit in one aligned 32-byte
region, but grows the table by 20%, increases generic write code, and changes
some register-save sequences. Stable `main` remains at `688ae7e`.

Measured with GNU Tools for STM32 14.3.1, C++17, Cortex-M7 hard float,
`-O2`/`-Os`, no exceptions/RTTI and no LTO. This is a host cross-compilation
experiment; no board, DWT, cache counters or cycle measurements were used.
The complete generated tables are in [MEASUREMENTS.md](MEASUREMENTS.md).

## Layout

All offsets and sizes are extracted from ARM objects, in bytes.

| Member/property | A: current | B: reorder | C: aligned immutable |
|---|---:|---:|---:|
| sizeof(Field) | 80 | 80 | 96 |
| alignof(Field) | 8 | 8 | 32 |
| get | 56 | 0 | 0 |
| set | 68 | 12 | 12 |
| cached type | absent | absent | 20 |
| cached flags | absent | absent | 21 |
| declaredType | 16 | 24 | 40 |
| id | 0 | 64 | 24 |
| name | 4 | 68 | 28 |
| unit | 8 | 72 | 32 |

The numeric type in A/B is the first member of FieldType, so dynamic reads
load it at offsets 16/24; C reads its cached tag at offset 20. This is also
visible in disassembly. C's name pointer is actually at offset 28, not 32 as
in the proposal's illustrative diagram; this does not displace the needed
numeric members from the first line.

For a 32-byte-aligned array base, the Field members needed for a read occupy
3/2 lines alternately in A, 1/2 in B, and 1 consistently in C. A/B only require
8-byte alignment; other valid base placements produce 2 lines consistently
for A and can produce 2 consistently for B. C's guarantee follows from both
alignment 32 and stride 96. These are address-range counts, not cache misses.
They exclude the index/catalog, dispatch tables, instruction literals, stack
and source-owner data, all of which can require separate memory accesses.

## Storage and code

The 1024-row Field array is 81,920 bytes in A/B and 98,304 in C: exactly
16,384 extra bytes. Probe .rodata grows by 16,392 bytes including padding.
All Probe and Json objects have zero .data/.bss and no startup constructors;
all three minimal consumers link with newlib-nano.

| Probe object | A | B | C |
|---|---:|---:|---:|
| .text, -O2 | 12,112 | 12,128 | 12,368 |
| .text, -Os | 11,902 | 11,910 | 11,936 |
| .rodata, -O2 | 82,177 | 82,177 | 98,569 |
| .rodata, -Os | 82,158 | 82,158 | 98,550 |

These totals include all 21 probes and emitted helpers. They are not a firmware
image-size estimate. Json .text is 10,352 bytes for all variants at -O2; at -Os
it is 6,966 in A and 6,958 in B/C. Names/enum strings and JSON behavior are equal.

## Read and write observations

- Known-ID matching reads are identical in size: 8-byte wrappers calling the
  same native getter for all three variants and both optimization levels.
  Layout contributes no additional gain to those already-folded accesses.
- Runtime Scalar read at -O2 is 2,428 bytes in all variants. At -Os it is
  2,252 bytes in A/B and 2,218 in C. However C adds `vpush {d8}` / `vpop {d8}`
  on that function's common path; A/B do not. Smaller code is not evidence
  of fewer executed instructions or lower latency.
- Dynamic float write grows from 1,044 bytes in A to 1,052 in B and 1,160 in C
  at -O2; -Os gives 1,080 / 1,084 / 1,118. Dynamic U16 write also grows in C.
  The split between unrestricted and restricted validation needs more code.
- C reads type/flags at offsets 20/21 and setter storage at 12..19.
  Unrestricted numeric writes avoid FieldType bounds; restricted F32 writes
  load limits at offsets 48/52. C's full-range floating checks can still read
  instruction literal pools or call a helper; one Field line is not one total
  memory access for the complete operation.
- Some known writes shrink at -Os: native F32 goes from 84 bytes in A/B to 40
  in C, bounded F32 from 84 to 64, and full-range U16 from 30 to 28. At -O2
  the corresponding sizes are equal across variants. The -Os C native-F32
  wrapper calls an outlined `scalarFinite<float>` helper, included in total
  .text; the shorter wrapper alone is not the complete operation's cost.
- Enum/plain U16 with equal bounds have equal wrapper sizes and corresponding
  numeric operations in every variant: reads 8 bytes; writes 36 at -O2 and
  56 at -Os. No enum description callback participates in numeric access.
- Direct lookup retains 16 static instructions in this fixture. At -Os its
  stride operand changes from 80 to 96; lookup gains no additional stage.

C combines immutability, cached tags/flags, alignment and a different numeric
validation path. Changes in its code cannot be attributed to alignment alone.
Inlining decisions here also differ from smaller standalone probes: claims
about these mixed 1024-field fixtures should not be generalized to all consumers.

## Compatibility finding

B/C preserve positional table rows through a constexpr constructor, but are
no longer aggregates. C additionally prevents member mutation and assignment;
public reads of `declaredType` and copy/move construction still work.

The first C prototype explicitly deleted both assignment operators. GCC 13/14
reported it trivially copyable, while Clang 18 reported otherwise, despite both
copy and move construction being trivial. A standalone reproduction with just
a const class member showed the same difference. Omitting those assignment
declarations retains the required trivial-copy trait on all tested compilers;
assignment remains disabled by the const members and declared constructors.
Five negative compilation cases guard the immutable contract. The final form
has identical ARM layout and section measurements to the first C prototype.

## Decision

The original condition "one Field line and the same or shorter runtime code"
does not hold for C across the tested operations. C provides the line-layout
guarantee, but several general paths are larger, and one read path gains FP
register saves. Without execution measurements it is not established that C
is faster overall, or that its larger table is worthwhile.

B is the smaller change and already puts Getter/Setter next to the numeric
descriptor header without increasing Field size. It is a candidate for later
measurement, not a proven faster replacement. This compiler-only evidence
retained A as the baseline and kept both candidates for reproducible comparison.

## Alignment controls and subsequent board decision

A32/B32 add only `alignas(32)` to A/B: both become 96 bytes. Every probe's
size and static instruction count stays the same as its unaligned counterpart
at both optimization levels. B32 fits the getter and numeric type within one
32-byte line for every row, without C's duplicated state or validation changes.
Aligning A alone does not bring its separated getter and type into one line.

B64 was also compiled: its 128-byte stride simplifies address calculation,
but exceeds the accepted per-field footprint and is excluded from production.
No board performance claim is made for that control.

The subsequent [H7S measurements](h7s/RESULTS.md) tested A/B/B32/C and selected
B32 for production: it improves large-table dynamic reads while retaining
the established read/write logic and public metadata/assignment behavior.
