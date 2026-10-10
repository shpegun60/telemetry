# Resource readability changes: retained H7S image equivalence

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[equivalence.json](equivalence.json) relates the current library and fixture
inputs to the twelve complete firmware images measured on H7S from sealed code
commit `7b73fb4c97e48ebb012ea0b6010bc7a4c3c434b3`. The five original hardware
receipts and the original offline receipt are preserved without changing their
source identity, measurements, completion flags or restoration records.

The current sources were captured and compiled using the existing H7S runners
without `--run`, CubeIDE ARM GCC 14.3.1 and each group's retained Cube scaffold.
The Mixed/Scale group contributes four images; descriptor, resources, exchange
and borrowed each contribute two. Every rebuilt firmware binary was compared
byte for byte with the actual retained artifact, its SHA-256 and its byte count
also checked against the original receipt. All twelve binaries match. Current
and retained ELF hashes are recorded separately: renamed C++ symbols can change
ELF metadata while leaving the firmware bytes identical.

The Exchange fixture embeds `__FILE__` in an assertion diagnostic. A fresh
capture directory initially changed that string and its related relocations.
The helper recompiles only `Benchmark.cpp` with an explicit
`-fmacro-prefix-map=<current inputs>=<measured inputs>` and links normally.
The mapping is retained in the new build's `images.json` and the equivalence
record. Binary files are never patched or normalized for comparison.

The 2026-10-04 documentation/style refresh also preserves three existing
assertion line constants in the shared Exchange fixture with explicit `#line`
anchors (107, 120, 134). Otherwise adding comments changes the diagnostic
constants embedded in firmware. The assertion expressions and refusal paths
remain identical. All twelve refreshed images were rebuilt from captured
current inputs and compared with the retained binaries; no original hardware
receipt or binary was edited.

The 2026-10-05 root-deduplication refresh rebuilds the same twelve images from
the current table, catalog and Model registry roots. Every raw firmware binary
still matches its retained measured artifact. This is offline compile/link
evidence for that capture's 131 library inputs and 57 fixture inputs, with no new
device execution or changed historical receipt.

The 2026-10-05 ergonomic API refresh captures 136 current library inputs and
57 fixture inputs and rebuilds the same twelve retained images. Every raw
firmware binary matches byte for byte after the documented Exchange source-path
mapping. The new field, native-call and resource-client headers are included
in the captured inventory; binary equality qualifies the existing fixtures,
which do not instantiate every newly added entrypoint. Their separate host and
ARM checks establish the new API behavior and code generation. This refresh
adds no device execution or new hardware measurements and preserves all six
original receipts. The final source capture includes the public-method comment
additions to the command/service tables and catalogs and rebuilds all five
groups from those exact inputs; its twelve firmware binaries remain identical.

The 2026-10-10 flat runtime native API refresh captures 138 current library
inputs and the same 57 fixture inputs. All twelve rebuilt raw firmware binaries
match their retained measured artifacts byte for byte after the documented
Exchange source-path mapping. Six current ELF files match the retained ELF
hashes; the six distinct ELF identities are recorded separately. All six
original receipt files remain unchanged, including their raw checkout bytes.
The capture records starting revision `00bab7dbcc6fb59e7e3b2c52d5f33aeca7b15192`
with `source_dirty=true` for the additive change; its manifests identify the
actual frozen inputs. The new flat result and Command status headers are part
of that inventory. Existing firmware fixtures qualify the APIs they already
instantiate; the new flat entrypoints have their own host and ARM checks in
[the ergonomic suite](../../ergonomics/README.md). The artifact verification
also passed the historical receipt controls, 27 equivalence mutation controls,
10 refused relocation mutations and the text-identity controls. This capture
is offline compile/link evidence and adds no device execution.

This establishes identical firmware bytes for these existing fixtures after
the resource formatting, include and namespace changes, private `FileEntry`
encapsulation, and cv classification fixes in `Model` and v3 descriptor metadata.
The new `BytesFile`, `FileView` and aggregate
include have separate host and ARM checks in [the resource suite](../README.md).
They are not instantiated by every historical firmware fixture. The record
reports compile/link only and does not claim a new hardware session or new
measurements for the demonstration application.

## Verification

```sh
python tests/resources/evidence/verify.py verify --self-test
python tests/resources/evidence/verify.py verify --self-test \
  --build-root "$EVIDENCE_OUTPUT" --retained-root "$EVIDENCE_RETAINED" \
  --path-map "$EVIDENCE_ORIGINAL_BUILD=$EVIDENCE_ARCHIVED_BUILD"
```

The first command is the CI gate. It checks the exact current library/fixture
inventory, the twelve distinct image identities, five receipt identities,
compiler, Cube scaffold identities and the offline receipt. Historical source
manifests must match the pinned Git commit's actual blobs. Original historical
coverage checks and mutation controls run from a temporary copy of that sealed
tree, with their normal source comparisons enabled. There is no general option
to skip source validation. CI uses a complete Git checkout so the pinned object
is available; the verifier never accesses the network.

Record format 2 uses explicitly named `receipt_lf_sha256` fields for all six
preserved JSON receipts. Only CRLF line endings are converted to LF before
hashing their original text bytes, so a Windows checkout and the Git LF blobs
have the same identity. The receipts are not parsed and serialized to calculate
these hashes: changed JSON whitespace, keys or measurement values still change
the digest. Current source manifests use the same LF convention. Firmware
binaries, ELF files, object files and compiler executables retain their raw
byte hashes and comparisons. Portability controls check both checkout forms,
changed text and distinct raw byte hashes; the publication gate also runs from
an exported Git index tree on Linux.

The second command additionally reads the retained and rebuilt binaries/ELFs,
captured inputs, copied Cube inputs and compiler executable. Missing artifacts
fail this mode. CI checks the captured evidence and current source hashes; it
does not rebuild with the Windows CubeIDE toolchain or run a board.

If generated build directories have been archived outside the repository, pass
their actual roots and an explicit path map. The original absolute image paths
in receipts/build provenance remain intact. From the repository root in
PowerShell, the retained archive and final publication capture can be verified with:

```powershell
$evidenceOriginalBuild = Join-Path (Get-Location) 'build'
$evidenceArchivedBuild = 'C:/Users/admin/Documents/telemetry-artifacts/2026-10-04-cleanup-98b945f/generated-builds'
$evidenceOutput = 'C:/Users/admin/Documents/telemetry-validation/20261010-flat-native/equivalence'
python tests/resources/evidence/verify.py verify --self-test `
  --build-root "$evidenceOutput" `
  --retained-root "$evidenceArchivedBuild/borrowed" `
  --path-map "$evidenceOriginalBuild=$evidenceArchivedBuild"
```

`--path-map OLD=NEW` is optional and changes only artifact path resolution during
verification or capture. It never moves, writes or rewrites the mapped files.
Both roots must be absolute, with distinct non-overlapping source roots. Each
matching recorded absolute path is mapped once, then checked to remain inside
its selected group's artifact directory. Relative paths keep that confinement
check. No hashes, source manifests, compiler flags or original recorded path
strings are rewritten. A missing/wrong map or a path that escapes the selected
group fails verification. Mutation controls exercise valid mappings, invalid
roots, prefix lookalikes and directory escapes. On Windows, an extended local
drive spelling such as `\\?\C:\...` is resolved as the corresponding ordinary
drive path before the same confinement checks; stored provenance is unchanged.

The equivalence mutation controls refuse a missing/changed input, group, family,
optimization, image, byte count, receipt, compiler, scaffold or path mapping.
The original historical controls still check their full required condition and
measurement counts.

## Reproduce the capture

Resolve the CubeIDE ARM GCC 14.3.1 executable on this machine. Use a fresh output
directory for each command to preserve the previous capture. Set `ARM_CXX` to
that executable, `EVIDENCE_RETAINED` to the archived `borrowed` directory and
`EVIDENCE_OUTPUT` to a fresh external output directory. Set
`EVIDENCE_ORIGINAL_BUILD` and `EVIDENCE_ARCHIVED_BUILD` to the absolute roots in
the PowerShell example when adapting these shell commands:

```sh
python tests/structured/mcu/h7s/run.py --arm-cxx "$ARM_CXX" \
  --cube "$EVIDENCE_RETAINED/h7s-own-mcu/scaffold" --output "$EVIDENCE_OUTPUT/mcu"
python tests/structured/descriptor/h7s/run.py --arm-cxx "$ARM_CXX" \
  --cube "$EVIDENCE_RETAINED/h7s-own-descriptor-live/scaffold" --output "$EVIDENCE_OUTPUT/descriptor"
python tests/structured/resources/h7s/run.py --arm-cxx "$ARM_CXX" \
  --cube "$EVIDENCE_RETAINED/h7s-own-resources-live/scaffold" --output "$EVIDENCE_OUTPUT/resources"
python tests/structured/exchange/h7s/run.py --arm-cxx "$ARM_CXX" \
  --cube "$EVIDENCE_RETAINED/h7s-own-exchange-live/scaffold" --output "$EVIDENCE_OUTPUT/exchange"
python tests/structured/borrowed/h7s/run.py --arm-cxx "$ARM_CXX" \
  --cube "$EVIDENCE_RETAINED/h7s-live-qualified/scaffold" --output "$EVIDENCE_OUTPUT/borrowed"
python tests/resources/evidence/verify.py capture \
  --build-root "$EVIDENCE_OUTPUT" --retained-root "$EVIDENCE_RETAINED" \
  --path-map "$EVIDENCE_ORIGINAL_BUILD=$EVIDENCE_ARCHIVED_BUILD" \
  --output tests/resources/evidence/equivalence.json
```

No command above supplies `--run`, an adapter serial, a serial port or a device
programmer. The historical capture layout used `build/borrowed`; archival
preserves that relative layout under the external generated-builds directory.
The current 2026-10-10 flat runtime native API capture is
`C:/Users/admin/Documents/telemetry-validation/20261010-flat-native/equivalence`.
The 2026-10-05 ergonomic API capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261005-ergonomics/equivalence/final-source`.
The first ergonomic capture remains in the parent `equivalence` directory.
The earlier 2026-10-05 root-deduplication capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261005-root-dedup/h7s-equivalence`.
The 2026-10-04 style-refresh capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261004-style-docs/h7s-equivalence`.
The earlier publication capture is retained as
`resource-current-h7s-paranoid-418-final` in the same external archive.
Reproduction uses the archive's scaffold paths, a fresh
external output directory and the explicit original-build-to-archive path map.
The Exchange compiler macro mapping still uses the measured literal source path, regardless
of where its artifact files are now stored. These local artifact directories are
retained evidence, not a permanent downloadable archive. Changing a source hash
requires another honest capture or a new hardware qualification; a successful
historical coverage check alone cannot update the current relation.
