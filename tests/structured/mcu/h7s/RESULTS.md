# Stage 14: H7S correctness, cycles and observed stack

Authors: Ruslan Kovtun (shpegun60), codexAi. Date: 2026-10-03.

The [retained receipt](receipt.json) records execution of source commit
`37bc85771cbbd8597e01835f1e8b187f7b7f32b3` on NUCLEO-H7S3L8,
ST-LINK `002A001F3033510135393935`, COM6. All 195 captured LF-normalized
source inputs were independently compared with that commit's Git blobs.
They match. The recorded `source_dirty=true` is preserved: the working tree
also contains 92 untracked review files outside these build inputs.

CubeIDE ARM GCC 14.3.1 built four independent Mixed/Scale images at O2/Os.
The MCU ran at 600 MHz with 64 KiB D-cache and I-cache enabled. Code and
immutable metadata occupy internal Flash; endpoint storage and Workspace
occupy AXI SRAM; the ID sequence and isolated probe stack occupy DTCM.
See the [measurement contract](README.md#measurement-contract) for warmup,
interrupt masking, the common loop and the independent PSP trampoline.

## Correctness and restoration

| Image | Conditions | Additional owning-return conditions | DWT windows | Stack observations |
| --- | ---: | ---: | ---: | ---: |
| Mixed O2 | 12230 | 3 | 266 | 234 |
| Mixed Os | 12230 | 3 | 266 | 234 |
| Scale O2 | 2831 | 0 | 154 | 138 |
| Scale Os | 2831 | 0 | 154 | 138 |

All **30128 conditions passed with zero failures**. Each Mixed count includes
the existing 97 multi-TU consumer conditions. Each image completed its full
operation/profile/repetition plan. The independent verifier checked raw UART
coverage, source-derived checksums, all descriptor bytes/FNV, captured input
and artifact hashes, and 70 receipt mutations. Compiler frames were checked
against the authenticated `.su` files separately.

The test runner saved and restored all 65536 bytes of internal Flash.
Fresh readback equals the backup byte for byte; both SHA-256 values are:

```text
a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456
```

Option bytes and external memory were not changed. The restored image was
reset and left running. Raw UART, images, captured sources, Cube scaffold,
maps, disassembly and `.su` files remain in the local artifact directory
`build/stage14/h7s-live1`. Their identities are in the receipt; this local
path does not promise a permanent downloadable artifact archive.

## 128-target runtime read comparison

These are medians in cycles per call for deterministic shuffled IDs. Each
value includes the common indirect probe call, ID sequence selection, loop
and checksum. Seven complete windows were measured. The legacy Scalar read
returns a native u32 through its existing erased API; the new encoded read
also writes canonical LE wire bytes, so they perform different final work.

| Operation | O2 cycles | Os cycles | O2 / Os observed stack, B |
| --- | ---: | ---: | ---: |
| Direct indexed owner control | 7.01 | 8.03 | 24 / 24 |
| New named visitor | 41.38 | 44.33 | 40 / 40 |
| New runtime `readAs<u32>` | 42.28 | 45.53 | 40 / 40 |
| New encoded u32 read | 61.22 | 50.01 | 68 / 56 |
| Legacy Scalar u32 read | 34.61 | 89.29 | 48 / 76 |

There is no universal speed win: the legacy runtime read is faster at O2;
the new native and encoded reads are faster at Os in this fixture. These
results are for 128 Flash-backed targets and this compiler/image layout,
not a replacement for the earlier RAM1024 measurements.

Fixed-position wrapper timings also expose a limit of instruction comparison. At O2 the
Scale direct-known u32 control measures 7.01 cycles, while new local/global
and legacy local wrappers measure 17.01/16.01/17.01. Mixed direct/local
Config reads measure 9.01/9.01, but global Config measures 18.01. Existing
standalone direct/local/global instruction-equivalence gates still pass.
Independent inspection of these actual linked benchmark bodies also finds
the same three u32 instructions and the same six Config instructions, with
the same owner data literals. No extra calls or result-packaging instructions
explain the timing differences. Function addresses/alignment differ; their
interaction with branch/cache/layout behavior is a possible explanation,
not an established cause. Equal instruction streams do not establish equal
cycles at different addresses in a linked image.

## Large objects and resource paths

Same-ID medians include the same common probe overhead. Service requests
are decoded completely before writing responses; the correctness body also
checks the large in-place response against every input byte.

| Operation | O2 cycles | Os cycles | O2 / Os observed stack, B |
| --- | ---: | ---: | ---: |
| Encoded 4 KiB Field read | 11980.30 | 10931.59 | 288 / 248 |
| Encoded 4 KiB Command | 3240.27 | 9381.85 | 244 / 280 |
| Encoded 4 KiB Service | 19204.30 | 31512.58 | 352 / 360 |
| Owning runtime `readAs<Big>` | 20562.30 | 20564.59 | 8256 / 8264 |
| Streaming descriptor, 128-byte slice | 2395.05 | 3135.20 | 116 / 260 |
| Packed descriptor, 128-byte copy | 457.06 | 460.14 | 48 / 56 |
| Whole 8322-byte Values file | 27784.27 | 25264.97 | 200 / 192 |

The owning native return really consumes approximately **8 KiB in the full
probe chain**, although the `nativeBig` root's individual `.su` frame is
4112 B. The nested owning-return work adds another large frame. For large
runtime values the encoded API uses caller-owned Workspace and bounds the
measured stack to hundreds of bytes. Returning a large owning C++ value is
not a zero-stack operation.

Packed descriptors trade precomputed immutable storage for lower read cost.
The 1775-byte descriptor and 8322-byte Values size are independently verified
from the fixture shape. The descriptor fingerprint is `25b585918d944874`.
This is a model fingerprint, not a hash of changing values.

Two fill patterns, three repeats and a 512-byte positive control make these
**observed stack writes including nested calls**. They exclude formatting,
UART, the caller's MSP frame and interrupts. They do not detect untouched
reserved stack slots or prove a worst-case bound for arbitrary callbacks.
All observed encoded large-object roots remain below the separate 1024-byte
full-probe qualification ceiling; the existing individual-frame gates are
unchanged.

No runtime library code was changed for this measurement. Namespace moves
and consumer migration remain subsequent work and require new final-source
qualification evidence.
