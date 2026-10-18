# Current Resource sources: retained H7S image equivalence

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

The owning-pointer and checked-codec refresh, captured on 2026-10-10 UTC,
includes the lvalue-only owning `ServiceCallResult::valueOrNull()` contract and
the additive codec helpers. Its inventory contains 138 library inputs and
57 fixture inputs; only `ServiceCallResult.hpp` and `Codec.hpp` differ from
the preceding flat native capture. All twelve raw firmware binaries remain
identical after the documented Exchange source-path mapping. Six current ELF
hashes match their retained counterparts; the other six are recorded separately.
All six original receipt files preserve their raw checkout hashes. The capture
honestly records starting revision
`6d6d3bf415e0c7e1ebb5f3c91c6521d38bfc5bb0` and `source_dirty=true`; its input
manifests identify the actual files compiled. Artifact verification and all
historical and equivalence mutation controls pass. New API behavior is qualified
by its separate host and ARM tests; these existing firmware fixtures do not
instantiate every additive helper. No device was accessed or measured.

The remaining owning-result pointer refresh was captured on 2026-10-11 local
time (2026-10-10 UTC). It makes `ServiceResult::valueOrNull()`,
`NativeCallResult::valueOrNull()` and `FieldReadResult` pointer access lvalue-only;
payload extraction with `value()` and `operator*()` remains available on rvalues.
The 138-library-input and 57-fixture inventory differs from the preceding capture
only in those three result headers. All twelve raw firmware binaries are still
identical to their retained measured artifacts; six ELF hashes also match, and
the other six remain separately recorded. All six original receipt files retain
their raw bytes. The capture records starting revision
`535e550a5675b5dd4a995b2a933075dae056f653` with `source_dirty=true`, rather than
claiming a future commit was compiled. Artifact verification and the complete
historical/equivalence mutation controls pass. Separate CubeIDE GCC 14.3.1
before/after probes preserve all 19 code/relocation sections, 15 stack frames and
six result-wrapper size/alignment/triviality profiles at `O2`, `Os` and `Og`.
These are offline compilation checks, with no new device execution or timing.

The 2026-10-11 Descriptor output-contract refresh changes only the
`DescriptorFile.hpp` comment and v3 provider README in its captured library
inventory. It retains 138 library inputs and the same 57 fixture inputs.
All twelve rebuilt raw firmware binaries match the complete retained artifacts,
their SHA-256 and byte counts after the existing Exchange compiler source-path
mapping. Six current ELF hashes also match the retained hashes; the other six
remain separately recorded. All six original receipt texts remain unchanged.
The capture records the actual starting revision
`5d2bb937880cb5c47da5399420469803d5ecf8ac` with `source_dirty=true` for the
documented contract change. Artifact verification, all historical receipt
controls, 27 equivalence mutations, 10 refused relocation cases and text-identity
controls pass. This refresh is offline compile/link evidence; none of its
commands accesses a device or adds a hardware measurement.

The 2026-10-17 additive RpcMailbox refresh captures 141 current library inputs
and the same 57 historical fixture inputs. The library inventory adds
`RpcMailbox.hpp`, `RpcMailbox.cpp` and `RpcWire.hpp`, and records the changed
provider README and explicit `resource_telemetry` source manifest. All twelve
complete raw firmware binaries were rebuilt with CubeIDE GCC 14.3.1 and match
the actual retained artifacts byte for byte, including their SHA-256 hashes
and byte counts, after the existing Exchange compiler source-path mapping.
Six ELF hashes also match their retained counterparts; the other six current
ELF identities remain separately recorded. The library and fixture hashes were
checked before and after all five build groups and remained unchanged. All
22 tracked receipt JSON files, including the six original receipts referenced
by this record, retain their raw checkout hashes. The source capture records
actual starting revision `7e848eca6f3a9588680f7a2a5296685492f58563` and
`source_dirty=true`; the manifests identify the compiled inputs. These existing
fixtures do not instantiate the additive mailbox API. Its behavior and offline
ARM checks belong to [the RPC suite](../../structured/rpc/README.md). This is
file-only compile/link evidence. Artifact verification, the historical receipt
controls, 27 equivalence mutations, 10 refused relocation cases and the
text-identity controls pass. No device was accessed, no new MCU measurement was
generated, and no new CI result is claimed.

The following 2026-10-17 RPC qmake opt-in refresh retains the 141-library-input
and 57-fixture inventory. Only `lib/resource/resource.pri` and the two Resource
READMEs differ from the preceding capture. The manifest selects RpcMailbox
separately with `resource_telemetry_rpc`; DescriptorFile/ValuesFile consumers
keep `resource_telemetry`. All twelve complete firmware binaries were rebuilt
with the existing offline runners and remain byte-identical to the actual
retained artifacts after the documented Exchange compiler source-path mapping.
Their SHA-256 hashes and lengths match the original receipts; six ELF hashes
also match and the other six identities are recorded separately. All 198
library/fixture input hashes remained unchanged through the capture, and all
22 tracked historical receipt JSON files retain their raw checkout hashes.
The capture records actual starting revision
`428ccfd22d953da90107ddc6d9aa4b5c62e0f8ed` with `source_dirty=true`.
Artifact verification, historical receipt controls, the 27 equivalence mutations,
10 refused relocation cases and text-identity controls pass. This refresh
accessed no device and adds no hardware measurement or CI result; it qualifies
the existing fixtures rather than the separate RPC execution fixture.

The 2026-10-18 tiny_delegate v1.2.1 refresh rebuilds all five groups and twelve
images from the clean published revision
`f615775bf3dd4c7c4e12ac047e60b111e52ce0dd`. The capture honestly records that
starting revision with `source_dirty=false`; its 141 library and 57 fixture
input hashes identify the compiled sources. Only `lib/delegate/README.md` and
`lib/delegate/tiny_delegate.hpp` differ from the preceding input inventory.
Every complete raw firmware binary matches its retained measured artifact,
SHA-256 and byte count after the existing Exchange compiler source-path mapping.
Six ELF hashes also match; the other six current ELF identities are recorded
separately. All 198 library/fixture LF input hashes remain unchanged through
the capture, and all 23 tracked receipt JSON files retain their raw checkout
identities. Artifact verification and
the plain CI verification both pass, including the historical receipt controls,
27 equivalence mutations, 10 refused relocation cases and text-identity controls.
The four archived Stage08 receipt self-tests also pass. This offline capture
uses the same CubeIDE GCC 14.3.1 executable and retained Cube scaffolds. It adds
no board execution, timing measurements or coverage for delegate paths that
these existing fixtures do not instantiate. Original hardware receipts remain
unchanged.

The following 2026-10-18 optional RpcClient/SessionPool refresh captures 145
library inputs and the same 57 fixture inputs from starting revision
`aff0f15edc9148850dd2235ee7e59908a1ced14d`, honestly recorded with
`source_dirty=true`. Four additive library files introduce the client, pool
and codec-only qmake selection; the existing Resource manifest and two provider
READMEs are also captured. All twelve complete raw firmware binaries match
the actual retained artifacts byte for byte, with the original SHA-256 hashes
and byte counts, after the same Exchange compiler source-path mapping. Six ELF
hashes match too, and the remaining six current ELF identities are recorded
separately. All 202 library/fixture input hashes remain unchanged before and
after the five build groups, and all 23 tracked historical receipt JSON files
preserve their raw checkout hashes. These old fixtures do not instantiate the
new optional APIs; their behavior is covered by the separate
[client](../../structured/rpc_client/README.md) and
[session](../../structured/rpc_sessions/README.md) suites. This is offline
compile/link evidence with no device execution or new timing measurements.
Both the artifact verification and plain CI verification pass, including the
historical receipt controls, 27 equivalence mutations, 10 refused relocation
cases and the text-identity controls.

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
$evidenceOutput = 'C:/Users/admin/Documents/telemetry-validation/20261018-rpc-extensions/equivalence'
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
The current optional RpcClient/SessionPool capture is
`C:/Users/admin/Documents/telemetry-validation/20261018-rpc-extensions/equivalence`.
Its `freeze-before.json`, `freeze-after.json` and `capture-summary.json` retain
the complete source/receipt stability checks and image identities alongside the
original runners' build artifacts. `artifact-manifest.json` inventories the
retained files by raw byte count and SHA-256; `verify-artifacts.log` and
`verify-ci.log` record artifact and source/control verification respectively.
The preceding tiny_delegate v1.2.1 capture is
`C:/Users/admin/.codex/artifacts/delegate-v1.2.1-telemetry-ci-fix-2026-10-18/equivalence`.
Its parent directory's `freeze-before.json`, `freeze-after.json` and
`capture-summary.json` retain the source/receipt stability checks and complete
image identities alongside the original runners' build artifacts;
`verify-artifacts.log` records the
artifact and mutation-control verification. The preceding RPC qmake opt-in
capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261017-rpc-p3-h7s/equivalence`.
The preceding additive RpcMailbox
capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261017-rpc-mailbox/equivalence`.
The preceding Descriptor output-contract
capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261011-audit-closure/equivalence`.
The preceding remaining-owning-result pointer capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261011-p3-lifetime/equivalence`.
The preceding owning-pointer and checked-codec capture remains at
`C:/Users/admin/Documents/telemetry-validation/20261010-owning-pointer/equivalence`.
The preceding 2026-10-10 flat runtime native API capture remains at
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

The 2026-10-18 portable asynchronous client refresh captures 165 library inputs
and the same 57 fixture inputs after moving RpcClient into the optional client
module and introducing the neutral Resource packet helper. All five existing
runners rebuilt twelve complete images without `--run`, using the authenticated
retained Cube scaffolds and the same CubeIDE GCC 14.3.1 compiler. Every raw
firmware binary matches its actual retained artifact byte for byte, with equal
SHA-256 and length, after the documented Exchange compiler source-path mapping.
Six current ELF hashes also match; all current and retained ELF hashes remain
recorded separately. All 222 source hashes remain unchanged through the
capture. All 23 tracked historical receipt files and all 24 retained binary/ELF
files preserve their original bytes. The record honestly identifies starting revision
`becf751ade63b269ac97b7622f3a9460d2861d31` with `source_dirty=true`.

This latest capture is retained at
`C:/Users/admin/.codex/artifacts/telemetry-async-client-2026-10-18/equivalence`.
Its `freeze-before.json`, `freeze-after.json`, `binary-comparisons.json` and
`capture-summary.json` retain source stability, historical byte preservation
and twelve independent complete-byte comparisons. `capture-commands.json`,
the five build logs, `verify-artifacts.log` and `verify-ci.log` record the
offline commands and successful artifact, historical and mutation controls.
These firmware fixtures do not instantiate the new asynchronous clients or
Encoded v1 envelope; their behavior belongs to the separate
[portable client](../../async_client/README.md),
[web client](../../client_web/README.md) and
[QML](../../codegen_qml/README.md) qualifications. This refresh adds no device
execution, hardware timing measurement or physical transport qualification.

The subsequent 2026-10-18 RPC handoff and web transport lifetime followup repeats
the offline capture with 165 library and 57 fixture inputs at starting revision
`39b9749e7d653228a20d09edffa73de15d5bec96`, honestly recorded with
`source_dirty=true`. Five fresh runner builds again produce twelve firmware
binaries identical to the retained bytes, SHA-256 and lengths; six ELF hashes
also match. All 222 source hashes, 23 historical receipt files and 24 retained
binary/ELF files remain unchanged through capture and verification. The new
`freeze-before.json`, `freeze-after.json`, `binary-comparisons.json`,
`capture-summary.json` and verifier logs are retained at
`C:/Users/admin/.codex/artifacts/telemetry-async-review-2026-10-18/equivalence`.
These fixtures still do not instantiate the asynchronous clients; this relation
adds no device execution or coverage of their transport paths.
