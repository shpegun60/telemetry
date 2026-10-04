# Stage 20 baseline H7S correctness, cycles and observed stack

Authors: Ruslan Kovtun (shpegun60), codexAi. Date: 2026-10-03.

The [archived baseline receipt](../../../../doc/evidence/pre-borrowed/h7s/mcu/receipt.json) records actual execution of source commit
`01fd180ff3678d7c47066f8a728dc1e02f0f3973` after source unification. CubeIDE ARM GCC14.3.1,
NUCLEO-H7S3L8, selected ST-LINK `002A001F3033510135393935`, COM6.
All 140 captured LF build inputs match that commit's Git blobs.
`source_dirty=true` is retained: the working tree also had the user's
unrelated 92-file untracked review tree. This is not a clean-tree claim.

The MCU ran at 600MHz with 64KiB D-cache and I-cache enabled. Code and
immutable metadata were in internal Flash, endpoint scratch/Workspace in
AXI SRAM, and the ID sequence/isolated PSP probe stack in DTCM.
The [measurement contract](README.md#measurement-contract) defines warmup,
interrupt masking, the common loop and the independent PSP trampoline.

## Correctness and restoration

| Image | Conditions | Owning-return extra conditions | DWT windows | Stack observations |
| --- | ---: | ---: | ---: | ---: |
| Mixed O2 | 12230 | 3 | 266 | 234 |
| Mixed Os | 12230 | 3 | 266 | 234 |
| Scale O2 | 2316 | 0 | 126 | 114 |
| Scale Os | 2316 | 0 | 126 | 114 |

All **29098 conditions passed, zero failures**. Mixed includes the existing
97-condition multi-TU consumer. Scale contains eight final-library operations;
only the two legacy-only controls were retired. Each image completed its
entire operation/profile/repetition plan. Raw UART bytes, independent
checksums, descriptor structure/bytes/FNV and full restore are validated.

All 65536 bytes of internal Flash were backed up, restored and freshly read
back. Both SHA-256 values equal:

`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`

The original image was reset and left running. Option bytes/external memory
were unchanged. Retained artifacts are in `build/stage20/h7s-qualified-mcu`;
their recorded hashes do not promise a permanent downloadable archive.
All loadable ELF sections were checked against the complete backup range.

## 128-target shuffled runtime read

Cycles include the common indirect probe call, ID sequence selection,
loop and checksum, with seven windows and no baseline subtraction.

| Operation | O2 cycles/call | Os cycles/call | O2 / Os observed stack, B |
| --- | ---: | ---: | ---: |
| direct_index | 7.01 | 8.02 | 24 / 24 |
| named_visitor | 41.39 | 44.35 | 40 / 40 |
| readAs_u32 | 44.28 | 44.63 | 40 / 40 |
| encoded_u32 | 61.14 | 50.00 | 68 / 56 |

The old Scalar/library comparison remains in the
[historical report](../../../../doc/evidence/pre-unification/stage14/RESULTS.md).
Its values are not relabelled as measurements of this final library. Different
linked function addresses can change cycles despite equal instruction bytes;
neither instruction identity nor these data establish a universal speed win.

## Large objects and resources, same-ID profile

| Operation | O2 cycles/call | Os cycles/call | O2 / Os observed stack, B |
| --- | ---: | ---: | ---: |
| big_read | 11980.30 | 10931.59 | 288 / 248 |
| big_command | 3240.27 | 9381.85 | 244 / 280 |
| big_service | 19204.30 | 31512.58 | 352 / 360 |
| native_big | 20562.30 | 20564.59 | 8256 / 8264 |
| descriptor_chunk | 2395.05 | 3135.20 | 116 / 260 |
| packed_chunk | 457.06 | 460.14 | 48 / 56 |
| values | 27784.27 | 25264.97 | 200 / 192 |

Large encoded paths use caller-owned Workspace. The owning native Big return
uses approximately 8KiB in the full application/probe chain; it is not a
zero-stack operation. The default local-object budget stays 32B, chosen at
compile time, with the same validation/wire semantics.

The stack figures are observed writes including nested calls, measured with
two fill patterns, three repeats and a volatile 512B positive control. They
exclude formatting/UART, the caller's MSP frame and interrupts. They do not
measure untouched reserved stack slots or prove an unconditional worst case.
Encoded large-object observations remain below the 1024B full-probe ceiling;
individual `.su` frame gates are checked separately.

Descriptor/Values bytes and fingerprint remain the frozen mixed fixture:
1775B descriptor, 8322B Values, fingerprint `25b585918d944874`.
Separate fresh Descriptor/Values/Bind-Exchange H7S receipts cover those
consumer suites at the same code commit. Exact published-SHA CI is a separate
publication gate.

These baseline numbers are not measurements of the borrowed extension. The current [receipt](receipt.json) and [extension qualification](../../../../doc/BorrowedNativeValues.md#qualification-results) record the later code separately.
