# Additive API qualification: 2026-10-05

[Runner and reproduction](README.md) · [Captured results](results.json)

The capture is pre-publication: `source_head` is the clean starting revision
`e263aa48a5afdb4158d6bb03543a6348de5a387c`, with `source_dirty=true` for these
additive changes. All three runs captured the same 75 source/test inputs before
building and required their byte hashes to remain unchanged afterward. The
record retains compiler hashes, flags, source hashes and the full summary
hashes. Its source hashes describe raw checkout bytes; the separate firmware
equivalence record uses normalized LF source identities.

## Executed qualification

| Configuration | Commands | Intended compile rejections | Outcome |
| --- | ---: | ---: | --- |
| Qt MinGW GCC 13.1, C++20, O2, null-checks flag | 99 | 79 | Pass |
| WSL Clang 18, C++20, O2, ASan/UBSan | 99 | 79 | Pass |
| CubeIDE ARM GCC 14.3.1, Cortex-M7, O2/Os/Og | 172 | 79 | Compile/inspection pass |

Each host configuration executed Fields (263 checks), NativeCalls (53),
ResourceClient (443), NativeScale-128 (644) and NativeScale-256 (1284).
Basics and Traversal also executed assertions but do not advertise a numeric
total. ARM did not execute these programs on a device.

## Runtime native selection

Both 128- and 256-target fixtures use groups of 32, with shared Request/Reply
types and distinct targets in each family. No compiler budget override is used.
Every row is exercised on the host, including selection failures. The fixtures
contain both manual visitors and convenience APIs, so their combined section
totals in the JSON are not a production Flash overhead measurement.

The named wrapper observations are identical at both target counts:

| Wrapper | O2 code / individual frame, B | Os code / individual frame, B |
| --- | ---: | ---: |
| Manual Command visitor | 56 / 16 | 48 / 16 |
| Command callAs | 56 / 16 | 56 / 16 |
| Manual Service visitor | 56 / 16 | 48 / 16 |
| Service callAs | 60 / 24 | 56 / 24 |

Convenience selection has its own routing status, and is not universally the
same code as a manual visitor. Native dispatch emits 256/512 entry thunks and
8/16 group thunks for the two families at 128/256 targets; the manual dispatch
also emits 256/512 entry thunks. Arbitrary giant monolithic tuples remain outside
this qualification: the initial MinGW fixture reached the known COFF string
table limit, and grouping follows the documented large-project pattern.

## Large output lifetime and stack

The 4 KiB owning Service probe constructs its result in caller-supplied final
placement storage, which must be sufficiently large/aligned and destroyed by
the caller. Native results still own their full payload size; an ordinary
automatic result variable may itself use that much stack.

| Wrapper | O2 frame, B | Os frame, B | Og frame, B |
| --- | ---: | ---: | ---: |
| Manual owning Service placement | 16 | 16 | 16 |
| Local callAs placement | 16 | 16 | 8 |
| Global callAs placement | 16 | 16 | 8 |
| Borrowed Field read | 0 | 0 | 8 |

The largest individual frame anywhere in that probe is 40/32/72 B for
O2/Os/Og; the runner rejects a frame larger than 256 B. Host tests separately
check final-address identity with a deleted-copy/deleted-move factory result
and borrowed Field address identity. These are individual frames and object
lifetime checks, not complete call-chain stack or device cycle measurements.

## Existing paths

Resource READ helper code sections and frame maps match an independent checked
manual encoder at O2/Os. Generic resource server sections and frames match the
retained `e263aa4` server at O2/Os. The separate
[firmware equivalence record](../resources/evidence/README.md) confirms all
12 retained binary images are unchanged, using the refreshed 136-library /
57-fixture inventory. Original device receipts retain their original identities;
this update did not access hardware.

Existing descriptor/resources, freeze, qualification, documentation examples,
source formatting and resource contract runners also passed locally. The new
ergonomic suite is wired into release, null-checks, sanitized and Cortex-M7 CI;
this pre-publication record does not claim a future exact-SHA CI result.

## Cross-endian client follow-up

The 2026-10-05 follow-up adds the existing ResourceClient.cpp/Protocol.cpp
program to the cross-endian runner, with production library sources unchanged
from `28da04d`. Local Linux little-endian execution and s390x execution through
QEMU both passed all 443 client checks. The complete runner passed 10 program
executions and five byte-identical Descriptor/Values files. Its source manifest
now also covers ResourceClient.cpp and endian.py itself; both target builds
assert their real native endian. The endian CI job runs this same gate and
retains both executable hashes, logs and source hashes in its summary.

This qualification is native/emulated host execution, not an H7S measurement.
The firmware equivalence record establishes 12/12 identical `.bin` images;
current/retained ELF hashes are separate identities, with only 6/12 equal.
