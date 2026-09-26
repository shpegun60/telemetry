# Positional tables, ABI 6

The current API has one declaration form per entry kind:
`FieldTable{field(...)...}`, `CommandTable{command(...)...}` and global
`FieldCatalogTable` / `CommandCatalogTable` containing `group(name, table)`.
The playground declares all sources and tables directly in
[DemoCatalog.h](../../app/demo/DemoCatalog.h), without a wrapper class or
table-building helper functions. Public `makeField` and `makeCommand` are
removed. The internal descriptor builders live under `detail/`.

Positions determine IDs. Neither definitions nor descriptors store field,
command or group IDs. Reordering entries changes the wire IDs; use
`reservedField()` / `reservedCommand()` to retain retired positions.
For unchanged ordered definitions, that ABI 5-to-6 refactor preserved JSON and
schema fingerprints. Later changes deliberately added `meta` and `f`.

## Verification

Run the standard [host and ARM runners](../README.md) from the repository root.
They include the new table parity suite, 26 new rejected programs, standalone
headers, layout guards, independent ABI archives and mixed-layout rejection.
The new suite verifies native/dynamic conversion parity, side-effect counts,
all supported callback forms, inherited and const owners, mutable closures,
enum ranges, Scalar fallback, reserved positions and global routing. The total
number of numeric checks can differ between Windows and Linux integer ABIs.

On CubeIDE GCC 14.3.1, Cortex-M7, C++17, both `-O2` and `-Os` pass the
30-source / 12-probe ARM runner. For six field operations, the direct reference,
local `read/write<I>()`, and global `read/write<makeId(...)>()` have equal
normalized instruction streams. GCC's single-branch aliases at `-Os` are
resolved before comparison. These cases cover F32 reads, F32-to-U16 reads,
bounded F32 writes, integer-to-F32 writes, full-range U16 writes and enum writes.
Local and global typed commands are likewise equal or a compiler alias.
No numeric typed field probe retains Scalar or erased callback dispatch.
The unknown-owner probe retains its necessary owner load.

This is not a claim that all firmware instructions are unchanged: runtime
catalog strides intentionally changed, and typed fields now bypass the old
Scalar adapter. The independent unchanged-wire test is:

```sh
python3 tests/position_tables/compare_schema.py --cxx g++ --build-dir build/schema-parity
```

This is a historical parity probe. It builds the selected worktree and pre-refactor commit
`b3f0fa6818293fd8e995ce0c53115d0a4a0ce0bc` without changing the worktree.
At the positional refactor, all three complete JSON documents (field schema, values and command schema),
including embedded CRCs, match. The JSONL SHA-256 is
`a6b9794513ad8653e1118f1869e830aa055e4b17c144ef72267307ba7a6b1c46`.
Git history containing that commit is required for this optional comparison.
It is expected to report a difference on the current tree because of the later
`meta`/`f` format changes. Do not remove these properties or ignore fingerprints
to make the historical comparison pass. Current wire correctness is covered by
the host JSON suites and resource binary goldens.

ARM32 layout is Field 96 bytes/aligned 32, Getter 8, Setter 8, FieldType 48,
Catalog 12, Command 20, CommandCatalog 12. Field offsets are getter 0,
readType 8, name 12, unit 16, setter 32, declaredType 40. The two hot cache
lines and `sizeof(FieldTable) == N * sizeof(Field)` for nonempty tables remain.
ABI version 6 and the exact tuple prevent old/new compiled modules linking
through the guarded APIs. Rebuild every consumer when upgrading.

## H7S command stride measurement

The [fixture](h7s/CommandStride.cpp) compares a natural 20-byte Command with
the same descriptor plus one reserved padding word (24 bytes). All other
library source files are identical between candidates. The runner uses the
copied H7S scaffold under `build/field_layout_experiment/h7s/scaffold`; it
does not modify the COBS project. Dependencies: Python 3, pyserial, the local
CubeIDE compiler and STM32CubeProgrammer. Defaults identify the existing H7S
probe and COM6; override `--serial` / `--port` for the same fixture board.

```powershell
python tests/position_tables/h7s/run.py --output build/command-stride
python tests/position_tables/h7s/verify.py --self-test
```

The measured board is NUCLEO-H7S3L8 at 600 MHz, I/D caches enabled. Tables
contain 128 or 1024 commands in Flash or AXI RAM; profiles repeat one entry,
walk sequentially or use a deterministic xorshift sequence. Each image is run
twice in ABBA order, with nine 32,768-call windows per profile: 432 windows.
Arguments are prepared Scalars; timing includes the lookup, validation,
indirect invocation, callback and checksum loop. This is a warmed steady-state
test, not a cold-cache worst-case bound or a measurement of an H753 application.

| Profile, 1024 rows | Natural 20 B | Padded 24 B |
| --- | ---: | ---: |
| Flash, sequential | 37.002 cycles/call | 37.025 |
| Flash, pseudo-random | 37.002 | 37.012 |
| RAM, sequential | 37.002 | 37.013 |
| RAM, pseudo-random | 37.002 | 37.003 |

No padding benefit was established. Production therefore retains 20 bytes,
saving 4096 bytes per 1024 command descriptors. This does not assert a global
latency optimum for every callback mix or cache state.

[receipt.json](h7s/receipt.json) and [samples.csv](h7s/samples.csv) retain the
complete measurement, original source identities and ELF/BIN/object hashes.
[rebuild.json](h7s/rebuild.json) records a build from the final refactored
source: **both candidate objects, ELFs and BINs match the measured artifacts
byte for byte**. A further live attempt stopped after its first session on a
programmer download error; its automatic restoration also passed. It supplies
no additional complete timing result. The offline verifier checks full ABBA
coverage, every checksum, summaries, candidate differences, final image
identity and restoration; nine modified evidence controls must be rejected.

The original 64 KiB firmware was restored and read back. Both hashes are
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
