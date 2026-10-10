# Flat native API qualification: 2026-10-10

[Reproduce](README.md#one-level-command-and-service-outcomes) ·
[Recorded inputs and measurements](flat-results.json) ·
[User API](../../doc/user/FlatNative.md)

The additive facade is measured against the existing detailed runtime API and
manual visitors in the same fixtures. It uses the existing indexed dispatch;
there is no new endpoint lookup or duplicate dispatch table.

This is a pre-publication capture: starting HEAD is `00bab7d`, with
`source_dirty=true`. The three final runs captured the same 61 input files
before compilation and required unchanged raw checkout hashes afterward.
The JSON preserves compiler identity, flags, input hashes, full-summary hashes,
object/executable hashes, named entrypoints, relocation targets and frame data.
Firmware equivalence uses its separate normalized-LF manifest. No device was
accessed; these are host results and offline Cortex-M7 compiler observations.

## Executed checks

| Configuration | Subprocess commands | Intended compile rejections | Outcome |
| --- | ---: | ---: | --- |
| Qt GCC 13.1, C++20, O2, null-checks flag | 70 | 57 | Pass |
| Linux Clang 18.1.3, C++20, O2, ASan/UBSan | 70 | 57 | Pass |
| CubeIDE GCC 14.3.1, Cortex-M7, O2/Os/Og | 99 | 57 | Compile/inspection pass |

Each final host run executes FlatCalls (671), FlatScale-128 (1,934),
FlatScale-256 (3,854), FlatLarge-1024 (79) and FlatLarge-4096 (79): 6,617 checks.
All builds disable optional copy elision. A separate strict GCC O0 capture also
passes the 671 lifetime/status checks. Host exceptions exercise factory failure
propagation; no-exceptions ARM builds retain the compile-time noexcept controls.

Existing detailed ergonomics tests pass on GCC and Clang ASan/UBSan
(99 commands, 79 intended rejections each); the old ARM gates pass too
(172 commands, 79 rejections). Service, Model, mixed endpoints, borrowed,
descriptor, resources, multi-TU qualification and freeze runners all pass.
The shared core runner executes 1,572,371 checks and retains 222 intended
rejections. Eight runnable user examples pass both GCC and Clang ASan/UBSan;
their ARM O2/Os builds link successfully without execution.

## Small response and status wrappers

Both populations use groups of 32, shared DTO types and distinct endpoints.
No compiler template/depth budget is increased. These named wrapper observations
are the same at 128 and 256 targets:

| Wrapper | O2 code / frame, B | Os code / frame, B | Og code / frame, B |
| --- | ---: | ---: | ---: |
| Manual Command | 56 / 16 | 48 / 16 | 40 / 16 |
| Detailed Command `callAs` | 56 / 16 | 36 / 16 | 40 / 16 |
| Flat Command `call` | 84 / 16 | 20 / 8 | 20 / 8 |
| Manual Service | 56 / 16 | 48 / 16 | 32 / 16 |
| Detailed Service `callAs<ServiceResult<T>>` | 68 / 24 | 40 / 24 | 48 / 24 |
| Flat Service `callAs<T>` | 68 / 24 | 40 / 24 | 60 / 40 |

Flat Command includes explicit routing/application status mapping. Its O2
entrypoint is 28 bytes larger. At Os/Og the smaller entrypoint calls helpers;
that column excludes the out-of-line mapping and is not a total-size saving.
Flat Service has no entrypoint-size/frame increase at O2/Os in these fixtures;
Og retains additional helper work. This is not a universal zero-cost claim or
a device cycle measurement.

Combined fixture sections include all three approaches, counters and assertion
paths. The JSON records O2/Os/Og `.text`/`.rodata` totals for both populations;
these totals do not establish incremental production Flash cost.

## Large owning and borrowed responses

Each large fixture uses an exact 1,024- or 4,096-byte payload. Caller-provided
aligned final storage is used for owning results. Deleted copy/move factory
controls verify direct construction; borrowed results retain address identity.
The native result still owns the payload, so an automatic result variable can
consume payload-sized caller stack storage. The facade avoids an additional
intermediate result; it does not eliminate storage for the response itself.

| Flat entrypoint, either payload size | O2 frame, B | Os frame, B | Og frame, B |
| --- | ---: | ---: | ---: |
| Owning, local / global | 16 / 16 | 8 / 8 | 24 / 24 |
| Borrowed, local / global | 0 / 0 | 0 / 0 | 40 / 40 |

All 16 optimized detailed/flat entrypoint pairs have identical instruction
section bytes **and** relocation offsets, types and targets. This comparison
covers owned/borrowed, local/global, O2/Os and both payload sizes. Independent
`objdump -r` inspection confirms the recorded relocation rows. Og differs and
is reported separately; flat owning wrappers use 24 B versus detailed 8 B,
while flat borrowed wrappers use 40 B versus detailed 24 B.

The maximum individual frame across each whole large fixture is 32/32/88 B
for 1 KiB and 40/32/88 B for 4 KiB at O2/Os/Og. The runner requires static
frames, named frame entries and no frame above 256 B. It refuses allocation
and hidden-copy symbol references in measured objects. These checks and
frames do not establish complete call-chain stack depth.

## Existing firmware and runner integrity

[The 12-image evidence relation](../resources/evidence/README.md) was rebuilt
offline from frozen library inputs: all 12 `.bin` images are byte-identical to
the retained controls; 6 of 12 ELF hashes match. Both ELF hashes are retained
for the remaining pairs. All six historical hardware receipts remain unchanged;
no hardware evidence was regenerated or relabelled as a new device run.

The refreshed relation captures 138 library and 57 fixture files. Verification
passes its 27 mutation controls, 10 path-relocation refusals and three positive
path controls, plus the historical receipt controls.

A failed-rerun control proves that an old `completed=true` summary becomes
incomplete before compilation starts. The runner inspects only objects built
in the current invocation. This prevents stale files in a reused output
directory from becoming apparent new qualification evidence.

CI runs the new runner independently of the existing ergonomics runner and
retains its summaries, diagnostics and ARM objects. These local observations
do not claim successful Actions completion for a future commit SHA.
