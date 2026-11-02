# Complete library line review, 2026-11-02

This is a fresh four-reviewer manual pass from
`e5226b3fdc8bbc9cb08cd25a5c0ea67d189c503b`, following the earlier
[audit](../library-audit-2026-11-02/README.md). It closes confirmed defects without
changing module boundaries, execution queues or wire formats.

## Actual coverage

All **221 tracked files under `lib/`, 47,537 baseline lines**, were manually read,
including vendored dependencies, emitted JavaScript, declarations, manifests,
README files and licenses. Changed library files were reread after correction.
The [coverage ledger](coverage.json) records current source identities, inclusive
read ranges and reviewers; its `source_commit` identifies the code checkpoint.
Windows checkout hashes are separate from LF-normalized source hashes.

The generated 9,078-line Boost.PFR header was split between two reviewers.
Lines 811–4500 and 4751–9078 used a reversible display abbreviation:
`C(X,Y) = detail::workaround_cast<T, decltype(X)>(Y)`. Every line, binding list,
identifier pair and ordering was still read; reverse expansion checked each
displayed line's text. Printed line endings were normalized, not claimed to be
raw byte output. Other ranges used the original source text.

Additional full manual scopes were the 16 generator source files (3,802 lines),
all 22 Qt/QML Inspector example files (7,162 baseline lines), and the root CMake,
module selection/install logic and complete CI workflow. This does not claim a
fresh line-by-line read of every test fixture or every historical receipt.
Coverage records attest to review; their hashes and range checks are not a
mathematical proof of correctness.

## Confirmed corrections

| ID | Reproduced issue | Correction |
| --- | --- | --- |
| A1 | A noexcept callback move changed a previously validated DynamicClient request before the FIFO copied it | Revalidate canonical payload after wrapper ownership and before the core copy. Maintained FieldWrite, Command and Service cases distinguish early mutation refusal from later mutation of already copied source bytes. |
| A2 | Workbench accepted a throwing lvalue callable copy through its noexcept wrapper | Require nothrow construction from the actual source category. Three intended compile refusals cover Apply, Execute and Service; move-only positive controls remain. |
| A3 | TCP FrameReader input overlapping its RX storage reached overlapping `memcpy`, or could overwrite unread input | Refuse whole-storage overlap before the first write, with `Invalid` and zero consumption. Preserve the existing partial frame and allow a later disjoint continuation. |
| T01 | Low-level Codec decode published/nullified its output pointer while that pointer's live bytes were still the wire input | Publish the decoded pointer, or null on refusal, only after the last input read. Canonical integer and noncanonical bool alias regressions detect both original failures. |
| T02 | `std::ref/cref(function)` failed table erasure, while a wrapped null function-pointer variable could be called | Reuse the exact typed runtime-function binding for functions. Borrow pointer variables but snapshot and check their target once before invocation. All endpoint families have maintained regressions. |
| D1 | A callable destructor rebound its owning delegate and the outer operation overwrote that new ownership without destruction | Guard each owner during target construction, inline relocation and destruction using existing metadata. Recursive reset is harmless; same-owner replacement is refused before consuming its argument. Both owning families have maintained lifetime tests. |
| D2 | An opt-in class-specific allocator returned null and owning assignment discarded the previous callback despite its failure guarantee | Check that preparation produced an invocable delegate before retiring the old target. A returning-assertion allocation test covers both owning families, lvalue/rvalue sources, inline/heap previous targets, and preserves ordinary empty-delegate move semantics. |

Each runtime correction has retained before/after repro evidence or a maintained
regression demonstrated to fail against the old implementation. The Workbench
diagnostic has three baseline-positive/corrected-negative compile controls.
The delegate change is an explicit local patch to the pinned dependency; see
its [provenance and contract](../../../lib/delegate/README.md).

An investigated legacy QML partial-RPC concern was **not** a defect. A native
22-check scenario proved that a newer transaction may replace a remote
`Receiving` transaction before final WRITE; it executes the endpoint once.
The existing legacy recovery contract was preserved.

## Qualification and measured limits

Maintained host suites cover GCC, Clang 18 ASan/UBSan, allocation traps and
negative API/compile controls. Targeted Codec and endpoint suites also compile
for Cortex-M7 at O2/Os/Og. DynamicClient, Inspector/Workbench and TCP suites
exercise the existing FIFO, backend correlation and ownership paths.

Selected Codec Cortex-M7 roots have unchanged object-symbol text and compiler
frames relative to the baseline at O2 and Os. For `decode_big`, the measurements
are 138 B / 24 B at O2 and 148 B / 40 B at Os. These are selected object roots
and static frames, not total firmware Flash or peak call-chain stack.

Dynamic submission adds one bounded canonical traversal after callable ownership,
with no additional payload buffer. The delegate guard adds no object fields,
heap policy or invocation fast-path changes. TCP overlap checks use bounded
address subtraction without summing potentially overflowing address extents.

For the selected delegate test translation unit, Cortex-M7 object text grows
from 488 to 640 B at Os and from 664 to 1,192 B at O2. O2 reset/replace/move
root frames grow by 8 B; the Os root frames remain the same, while outlined
reset/move helpers grow from 0 to 16 B. Invocation instruction bytes and measured
x86-64/ARM object layouts are unchanged. This is a bounded lifetime correction
with a measured code/stack cost, not a claim of a performance improvement.

The final qualification receipt is recorded separately from this manual-coverage
ledger. GitHub Actions must pass for the exact PR head and then the actual merge
commit; an earlier green head is insufficient. Detailed local repros, logs and
reviewer ledgers are retained outside the checkout in
`telemetry-line-review-2026-11-02`.

No board was programmed or reset during this pass. MCU compile/link evidence is
not hardware execution. Earlier UART/DMA receipts retain their own source
identities; this audit does not turn them into new hardware qualification.
The supported Telemetry configuration remains C++20. Reading the vendored legacy
C++14 and experimental C++26 paths does not claim those configurations were
compiler-qualified here.

## Use the library

Start with the [user guide](../../user/GettingStarted.md) or the
[Qt/QML Workbench](../../../examples/inspector_qml/README.md). The existing
Read → Edit → Write → Refresh, Command, Service and bounded scalar Watchlist
workflows remain the application entry points. No new wrapper, transport or
queue must be adopted for these corrections.
