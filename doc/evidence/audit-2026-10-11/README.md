# Audit follow-up and H7S qualification, 2026-10-11

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This record follows the two independent reviews of
[`5d2bb93`](https://github.com/shpegun60/telemetry/commit/5d2bb937880cb5c47da5399420469803d5ecf8ac).
That revision's GitHub CI completed successfully, with all nine jobs passing.
The follow-up changes contracts/documentation and adds test coverage. The only
production C++ change is an explicit caller-precondition comment in
[`DescriptorFile.hpp`](../../../lib/resource/telemetry/v3/DescriptorFile.hpp);
no production accessor, dispatcher, codec or copying implementation changes.

## Review observations

| Observation | Resolution |
| --- | --- |
| Owning accessor ref-qualifiers affect pointer-to-member types and mangled symbols | [Release note](../../ReleaseNotes.md) and [migration guide](../../StructuredTelemetryV3MigrationGuide.md) require a complete clean rebuild; matching firmware bytes do not promise universal C++ binary compatibility |
| Documentation navigation was only checked locally | A [tracked checker](../../../tools/documentation_navigation/README.md) runs in CI, with missing-file/anchor controls; seven retired index anchors were restored without rewriting historical qualification records |
| Packed descriptor copying requires disjoint storage | The entire READ output must be disjoint from immutable source/metadata; documented in the provider and [user guide](../../user/DescriptorAndValues.md#streaming-або-packed-descriptor). This remains a caller precondition, without a runtime alias check |
| JavaScript Number conversion need not preserve NaN representation bits | The [numeric/raw representation contract](../../user/DescriptorAndValues.md) covers F32/F64; tests preserve numerical NaN semantics and exact raw-byte/U32/U64 relay. Production JS codec is unchanged |
| Additional malformed-input coverage was requested | Independent finite byte-grammar and payload corpora were added to the existing maintained resource, endpoint and JS runners |

Owning `value() &&` still supports move-out; saving its reference does not extend
the result's lifetime. Borrowed lifetime, synchronization and custom-provider
contracts are unchanged. No automatic transport authorization or retry deduplication
is introduced by these tests.

## Host and offline compiler checks

The [resource boundary probe](../../../tests/resources/BoundaryCheck.cpp) checks
34,688 replies against an independent byte grammar and 5,000 guarded requests.
Its 306,653 conditions are included in the runner's 316,118 total. GCC 13.1,
GCC with retained null checks and Clang 18 ASan/UBSan each passed the full runner:
96 commands and 68 intended rejections. CubeIDE ARM GCC 14.3.1 passed O2/Os
compilation and the existing size/stack gates; it does not execute this corpus.

The [encoded payload probe](../../../tests/structured/endpoints/PayloadBoundary.cpp)
executes 10,742 packets and 427,986 conditions through compiled Field, Command
and Service boundaries. It independently checks exact lengths, three canonical
bool members, unknown enum codes, callback counts, unchanged input, guarded
output and preservation of an occupied Workspace/outer lease. All 4,479 canonical inputs
remain valid, including 2,903 with unknown representable enum codes. Full
GCC/retained-null-check and Clang ASan/UBSan endpoint runners passed; ARM
O2/Os/Og compilation and the existing native codegen gates passed.

The [JS interoperability suite](../../../tests/structured/client/README.md)
passed 8,889 computed conditions with Node 22.23.3 against GCC, GCC with retained
null checks and Clang ASan/UBSan C++ fixtures. This includes numeric F32/F64,
raw IEEE-bit relay, descriptor mutations with refreshed fingerprints, and Values
mutations. The finite corpora are repeatable checks, not exhaustive input proofs.

Provider correctness, independent descriptor/Values byte oracles and no-heap
checks passed on GCC and Clang ASan/UBSan; provider ARM O2/Os/Og gates passed.
The [image-equivalence record](../../../tests/resources/evidence/README.md)
was refreshed: all twelve rebuilt firmware `.bin` files remain byte-identical
to their retained artifacts. Six ELF files also match; the others are recorded
separately. Historical receipts retain their original bytes and source identity.

## New hardware execution

The explicitly selected ST-LINK `002A001F3033510135393935` was identified as
NUCLEO-H7S3L8; the UART was COM6. CubeIDE ARM GCC 14.3.1 built every image and
bounded every Flash section before device access. The established scaffold
runs Cortex-M7 at 600 MHz with caches enabled.

| Images | Result | Retained receipt |
| --- | --- | --- |
| Mixed O2 and Os | Each: 12,230 correctness conditions plus three owning-Big comparisons; full timing/PSP plans passed | [MCU receipt](h7s-mcu-receipt.json) |
| Scale O2 and Os | Each: 2,316 correctness conditions; full timing/PSP plans passed | [MCU receipt](h7s-mcu-receipt.json) |
| Resource O2 and Os | Byte-equivalence `check=0`, descriptor/Values timing plans complete | [Resource receipt](h7s-resources-receipt.json) |

Both sessions independently backed up all 65,536 internal Flash bytes, verified
each programmed image, restored the backup and verified a fresh full readback.
Both backup/readback SHA-256 pairs are identical:

```text
a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456
```

The original firmware was reset and left running. Option bytes and external
memory were not written. The receipts honestly record starting HEAD `5d2bb93`
and `source_dirty=true`; captured input hashes identify the actual compiled
sources rather than a future commit. The MCU artifact verifier and its 71
mutation controls passed; the resource receipt's 16 controls passed. These
new records do not replace or relabel earlier measurements.
CI rechecks their historical coverage/control contracts without claiming a new
board run or treating later library changes as hardware-qualified.

The H7S fixture uses synchronous callbacks and its established DWT/PSP probe.
The watermark measures observed stack writes, not untouched reserved slots or
a worst-case bound. It does not qualify simultaneous H753 DMA, Ethernet,
FreeRTOS tasks or unsynchronized callback rebinding. That experiment belongs
to the consumer firmware with its real synchronization and transport setup.
The new host input corpora were not flashed as H7S images.

## Rechecking the retained records

From the checkout root:

```sh
python tests/structured/mcu/h7s/verify.py --receipt doc/evidence/audit-2026-10-11/h7s-mcu-receipt.json --self-test
python tests/structured/resources/h7s/verify.py --receipt doc/evidence/audit-2026-10-11/h7s-resources-receipt.json --self-test
python tools/documentation_navigation/check.py --self-test
```

Use `--artifacts` with the MCU verifier to authenticate retained image/backup/
readback files. If library sources later change, `--allow-stale-inputs` checks
the MCU record's historical consistency without claiming current-tree coverage.
These verification commands do not operate the board.

External build outputs and full logs are retained under
`C:/Users/admin/Documents/telemetry-validation/20261011-audit-closure` (hardware,
provider and equivalence), `20261011-audit-boundaries` (C++ corpora), and
`20261011-audit-5d2bb93` (JS/tooling). They are local artifacts, not a permanent
downloadable archive.
