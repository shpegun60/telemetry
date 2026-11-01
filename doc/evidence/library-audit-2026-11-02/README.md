# Library audit, 2026-11-02

This audit closes reproducible defects found after the optional Encoded Mixed
Batch implementation. It preserves the module boundaries, existing FIFO and
wire formats. It does not add an exactly-once execution guarantee.

## Review scope

Baseline: `87b5433f77e3028770db2ad75b54a317ef4f0509`.
Four agents reviewed all 221 tracked `lib` files, all examples, the Qt playground,
the generator and selected build entry points: 388 files / 75,479 baseline lines.
The [coverage ledger](coverage.json) records file identities and actual coverage.
Its canonical Git fingerprints and original Windows checkout fingerprints are
separate because the checkout contains CRLF and mixed line endings.

387 files were read in full. The generated Boost.PFR `core17_generated.hpp`
was manually read at lines 1–140 and 9041–9078; its complete 9078-line body was
checked byte-for-byte against the pinned upstream generator and structurally
validated across all 202 overloads. The other 8900 lines are not claimed as a
manual read. Test fixtures were inspected selectively, including every new
regression and the affected lifecycle tests; this is not a line-by-line audit
of every repository test.

## Corrected defects

| ID | Observed failure | Correction and maintained regression |
| --- | --- | --- |
| B1 | A typed null function-pointer completion could be accepted and called after a real operation | Pointer-only rejection before reservation in five submission layers; existing Async, DynamicClient, Inspector and ReadMany suites verify no queued work or snapshot retirement. Callable bool conversion is not an admission test. |
| B2 | Moving a callback could replace the caller's Inspector binding or change selection while the next revision was being published | Copy the binding and guard ownership publication with the existing scope helper. Reentrant progress is suppressed, while close/invalidation remain effective. Inspector tests exercise move/destructor reentry and revocation. |
| B3 | ReadMany opt-in after Encoded's qmake include omitted optional implementation sources | Move optional source selection outside the one-time include guard and deduplicate it. Twelve source-inventory cases cover both Batch/ReadMany orders and repeated includes. These inventories are not compilation of all twelve configurations. |
| C01 | Owning delegate assignment destroyed a callable subobject before copying it | Prepare the replacement while the source is alive. The default inline case produced a wrong value; heap-enabled cases produced ASan use-after-free. Both delegate families have maintained regressions. |
| C02 | Move assignment from a same-type delegate embedded in the old target read freed source storage | Stage the source before destroying its enclosing target. The heap regression reproduces the original ASan failure. |
| A1 | The public Nucleo build script allowed an existing empty output directory, then failed while creating it | Preserve the nonempty-directory refusal and allow the empty directory. The offline mock runner now has 33 controls, including both cases; no programmer is opened. |
| A2 | A delayed browser File read could display an old Service response after selection or Descriptor replacement | Bind file/network completion and error publication to a display revision. Real Chromium tests cover selection ABA, identical Descriptor reload and stale errors. |
| R1 | Qt Resource bootstrap assumed READ cursors were byte offsets | Forward opaque tokens verbatim; bound received bytes independently of the cursor and require exact STAT size at EOF. Support cursor-only progress with a 4096-reply budget per file, without extending discovery deadlines. |

The delegate correction is a documented local patch to the pinned dependency;
see its [provenance](../../../lib/delegate/README.md). Borrowing a member of an
owner that is then destroyed still violates the existing borrowed-lifetime
contract. No ownership mode is silently converted to another one.

## Qualification

The same maintained callback/binding, delegate and browser regressions fail
against baseline inputs and pass with the corrections. Additional standalone
ASan repros exercise the original delegate lifetime failures. A Qt bootstrap
repro uses a real 900-byte Descriptor and a non-offset READ token.

Final local evidence uses fresh external build directories and retained
compiler/runtime logs and source fingerprints.

A supplementary native Windows Qt 6.10.1 run exposed a Watchlist test-harness
race: adding a row expanded the panel, but the next simulated click used its
pre-polish position. The button received zero clicks, so no request was submitted.
The maintained helper now settles pending layout polish before the real mouse
click. The original completion assertions and strict warning checks remain;
application scheduling and test timeouts are unchanged.

Relevant maintained gates:

- Async, DynamicClient, Inspector/Workbench and ReadMany with Clang 18 ASan/UBSan,
  no-heap controls, negative API probes and Cortex-M7 compile/symbol checks.
- Inspector Host/NoHeap: 1313 checks each; FieldWrite: 1647 each; Command: 980 each;
  Service: 1228 each. Existing FIFO and backend lifecycle tests remain enabled.
- Mixed Batch: Wire 15,327, Server 87, Client/NoHeap 436 each and CrossGroup 223;
  six deliberate allocation traps and negative API controls remain enabled.
- Shared registered delegate checks: default no-heap 50 and opt-in heap 112,
  including allocation/construction failures and source cv/ref categories.
  The full shared Clang sanitizer gate executes 1,572,541 runtime conditions,
  227 expected rejections and six intentional abort controls.
- Real Chromium 1243 / Playwright 1.63.0: 30 browser checks; actual JS/native
  codec integration: 8889 checks, zero failures.
- Qt 6.4.2 QML/controller: 1921 checks and read/edit offscreen smokes. TCP with
  Clang ASan/UBSan: 812 checks, 36 reads, 32 connections and a real QML smoke.
  The maintained Qt runner excludes LSan for global offscreen allocations.
- Serial with Clang ASan/UBSan: 4593 bootstrap checks and 220 real PTY checks,
  including the real CLI. PTYs are host-created and are not hardware ports.
- Independent installed/sealed module consumers, relocation, negative compile/
  link controls, Cortex-M7 compilation, formatting and documentation navigation.
  All 20 relocated consumers execute with GCC; all 20 compile/link for
  Cortex-M7. Both module profiles pass 278 negative-include controls with their
  227 recorded source hashes unchanged.

GitHub qualification is tied to the exact published PR head and then the merge
commit, separately from these local receipts. A prior green SHA is not evidence
for a later revision. Detailed external evidence is retained in
`telemetry-full-audit-2026-11-01`; the published ledger contains no original
device Flash, binary dumps or private transport data.

## Measured costs

Inspector/Workbench object sizes are unchanged on the measured x86-64 and
Cortex-M7 ABIs. Freezing the binding increases the existing Cortex-M7 `-O2`
`inspector_read` local frame from 728 to 776 bytes. Child, decimal and handoff
probe frames stay 152, 144 and 24 bytes. These are local `.su` frames, not peak
stack estimates.
The Qt bootstrap remains 88 bytes on the measured x86-64 / Qt 6.4.2 ABI;
the new cursor-progress counter uses existing padding.

Prepared delegate replacement adds one noexcept transfer for ordinary inline
assignment. Object layout, invocation and heap policy stay unchanged. Across
eight Cortex-M7 `-Os` object roots, text changes from 872 to 1040 bytes; this is
not a linked firmware Flash delta. With inline capacity 64, measured caller
frames change from 32 to 96 bytes for callable copy assignment and 16 to 96
for delegate move assignment. Capacity 16 gives 32 to 40 and 16 to 48 bytes.
The temporary is bounded by delegate capacity; the old complete-target heap
alias path's target-sized stack temporary is removed.
With heap fallback explicitly enabled, preparation can temporarily keep both
the old and replacement heap allocations alive. The default no-heap profile
continues to allocate nothing.

## Contract boundaries

No board was programmed or reset for this audit. Earlier hardware receipts
retain their original source identities. Long-duration DMA/cache stress and
qualification of a different board or writable UART profile remain separate
hardware work, not results inferred from host sanitizers.

Mixed Batch remains optional Encoded functionality; RPC batching is not implied.
Configured retrying backends can repeat operations according to their contract.
The Workbench does not automatically replay an uncertain mutation. Owner-only
runtime interfaces remain owner-only, and borrowed buffers still need explicit
lifetime and physical transport release.

The review and tests reduce known risk within those contracts; they do not prove
the absence of every possible defect. No confirmed audit finding is deferred as
a future feature.
