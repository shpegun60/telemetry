# FieldWrite Workbench

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This contract scopes the first writable Workbench to
**Read → Edit → FieldWrite → Refresh**. Commands, Services, batch operations,
new wire formats and physical programming are separate work.

## Ownership and compatibility

One owner loop controls one DynamicClient and its existing FIFO per session.
The shared snapshot component stores a copied canonical value, identity and
revision, and provides checked typed navigation. It owns no transport, client,
queue, thread or dynamically allocated storage. Inspector and Workbench reuse
that implementation; the existing read-only InspectorModel API remains valid.

The editor uses separate fixed or caller-owned staging storage. Schema storage,
workspace control objects, callbacks and transport buffers retain their existing
lifetime contracts. Close/drain completes logical requests; it does not itself
prove physical TX release or remote session closure.

Every navigation and editing operation checks owner, admission, revision and
schema generation before metadata or payload access. A new schema/session,
selection, accepted read or invalidation retires the appropriate handles.
Handles must not outlive their owner.

## Editing and submission

Editing starts from a successfully read canonical value of a writable Field.
The editor binds that Field, root type, schema/session identity and snapshot
revision. It uses DynamicValueWriter internally; GUI users never build payloads
or request identities. There is no native DTO generation at runtime.

Invalid text, overflow, wrong scalar type or stale identity leaves local value
bytes unchanged. U64/S64 text never passes through double or QML Number. Editing
one leaf preserves all other bytes, including NaN payloads and signed zero.
Structs and arrays are edited through their checked scalar leaves; enums use
their exact underlying type, including unknown open-enum values.

An initially refused Write submission leaves the draft and published snapshot
intact. A committed payload is retained independently of a consumed Writer so
QueueFull does not silently discard the user's edits. Accepted writes use the
same FIFO and admission/handoff guards as reads. There is no automatic mutation
retry, extra scheduler or second client.

Both Write and Refresh timeout budgets are checked before submission. Invalid
budgets leave the draft intact and never start a remote mutation. A full FIFO
can still refuse the later Refresh after a confirmed Write; the result records
that separately. In particular, a one-slot client cannot enqueue a readback
from a Write callback while that slot is still completing; choose at least two
slots for a self-contained Apply/Refresh workflow.

## Portable API

Include `<integration/workbench/WorkbenchModel.hpp>` and link the optional
`Telemetry::workbench` CMake component, or include
`lib/integration/workbench/workbench.pri` in qmake. Backend and owner clock are
the same application objects used by DynamicClient. Do not create a separate
Inspector alongside Workbench for the same session: Workbench already exposes
the Inspector read/navigation interface and privately owns it.

```cpp
namespace wb = telemetry::integration::workbench;
namespace rt = telemetry::runtime;
namespace cl = telemetry::client;

wb::WorkbenchModel<> workbench{backend, clock};
// Admit schema/header/session, select a Field, then refresh through the usual
// owner process/transport loop. After that read has published a snapshot:
if (workbench.beginEdit() != rt::Status::Ok)
    return;

// gainNode is a checked child handle obtained from root()/child(). Use the
// exact scalar type from info(gainNode); no implicit numeric conversion.
if (workbench.setScalar(gainNode, 12.0f) != rt::Status::Ok)
    return;

const auto submitted = workbench.apply([](const wb::ApplyResult& result) noexcept {
    if (!result.writeCompleted || !result.write.succeeded())
        return; // Inspect result.write: refusal, timeout or OutcomeUnknown.
    if (!result.refreshCompleted || !result.refresh.published)
        return; // Write succeeded; refreshSubmission/refresh explain readback.
    // The owned snapshot now contains the actual device value.
});
if (!submitted) {
    // The draft remains editable; handle submitted.status before retrying.
}
```

The caller continues the same owner loop after submission. No synchronous
wait or hidden thread is introduced. Completion must be noexcept and fit the
configured inline callback storage. It runs once for an accepted Apply.
`drained()` remains false while that completion is running. Handles and
callbacks must not outlive the Workbench; destruction requires logical drain
and actual transport-buffer release.

`beginEdit()` validates/copies the published value into fixed staging storage.
`setScalar()` uses a checked prepared handle and changes only one leaf.
`editScalar()` previews that draft, while ordinary `scalar()` continues to read
the published snapshot. `dirty()` compares draft bytes on demand; `canApply()`
requires an actual change. `cancelEdit()` discards only the local editor.
Accepted Apply retires the published snapshot, submits one Write and retains
the committed payload until its completion. It publishes no local draft as a
device value.

`PayloadBytes` bounds both request capacity and each of the two edit buffers;
`SnapshotBytes` independently bounds the read snapshot. Only Workbench pays for
the draft, committed request, Writer and Apply completion storage. There are
no dynamic allocations, virtual interfaces or additional FIFO in these classes.
The [Qt example](../examples/inspector_qml/README.md#read-edit-write-and-refresh)
provides the complete runnable GUI workflow.

## Apply results

Apply submits one Write. Only a confirmed successful Write schedules one normal
Refresh through the same FIFO. The two outcomes remain independent:

| Write | Refresh | Reported state |
| --- | --- | --- |
| Submission refused | Not submitted | Draft preserved; explicit submission error |
| Endpoint refused | Not submitted | Write refused; no success indication |
| Confirmed successful | Successful and current | Publish the actual device value |
| Confirmed successful | Refused, failed or stale | Write confirmed; readback unavailable |
| OutcomeUnknown | Not automatically retried | Value may have changed; recovery required |

A Refresh failure, including QueueFull, never rewrites a confirmed Write as a
failed Write. A stale schema prevents value publication without erasing the
known operation result. Local edited bytes are never published as confirmed
device data. Read-modify-write does not provide compare-and-write or protection
against concurrent application changes.

The Qt controller consumes the terminal Apply result before retiring either a
Demo or TCP session. Explicit Disconnect preserves Cancelled before handoff,
OutcomeUnknown after uncertain handoff, and a known successful Write when its
Refresh is interrupted. During explicit disconnect or live-session replacement,
the accepted Apply emits one terminal signal outside the core, before a
reentrant state-change handler can replace its session.
Disconnect clears cached values; a readback already completed remains a known
historical result. No packet is handed off during retirement.

## Application and transport scope

The Qt application owns input parsing, labels, QML models and connection policy.
It supplies a writable Host Model with a normalizing setter for the first
end-to-end workflow. Existing read-only Demo and Inspector scenarios remain
available. Remote writes require both Descriptor permission and explicit
application opt-in.

The existing NUCLEO-H7S3L8 firmware and serial profile remain read-only in this
stage. Their validated read-only reconnect proof must not be generalized to
uncertain mutations. The portable Workbench remains backend-independent; a
writable UART profile needs its own controlled peer and qualification.

The current 2048-byte serial body budget and wire formats remain unchanged.
Future writable profiles must budget the actual request and response airtime,
backend deadline and processing allowance before submission.

## Acceptance evidence

Maintained host tests cover the real Read/Edit/Write/Refresh path, device-side
normalization, structs/arrays/enums, U64/S64 limits, unchanged floating-point
bits, read-only permission, invalid input, capacity refusal, stale identities,
callback reentrancy and destruction. Timeout tests use a caller clock and explicit
queued/offered/prepared states; an elapsed wall-clock sleep is not a handoff proof.

Write success followed by Refresh failure is tested separately from Write
failure. Invalidated edits and uncertain writes cannot cause an automatic new
handoff. No-heap probes and existing Inspector compatibility suites remain gates.

Qt tests exercise actual QML edit and Apply handlers plus readback, not only C++
helper calls. GCC, Clang ASan/UBSan, installed-module and offline ARM checks retain
raw reports outside the source tree. RAM/Flash/stack claims identify their exact
toolchain and measured inputs. Physical hardware qualification is not implied by
host or CI results.

## Measured storage and compiler probes

On x86-64 GCC 13.3, with CallbackBytes=256, extraction changes the existing
Inspector by a fixed 64 bytes. NodeHandle remains 48 bytes. Workbench adds two
PayloadBytes buffers plus 592 bytes of editor/completion state on this ABI:

| Configuration (N, PayloadBytes, SnapshotBytes) | Inspector before | Inspector shared store | Workbench |
| --- | ---: | ---: | ---: |
| 2, 64, 64 | 1184 B | 1248 B | 1968 B |
| 8, 4096, 65536 (Qt example) | 72800 B | 72864 B | 81648 B |

These are complete model object sizes, excluding schema indexes, backend,
transport buffers and Qt objects. They are ABI-specific measurements, not
portable size promises.

Using ARM GCC 13.2.1, Cortex-M7/Thumb, C++20, -Os, no exceptions/RTTI and heap
fallback disabled, the unchanged `tests/inspector/Standalone.cpp` probe has
8063 B object text at baseline d3efaca and 8245 B after extraction (+182 B).
Its largest individually recorded compiler frame changes from 392 to 384 B.
The separate Workbench Standalone probe records a 656 B frame for prepared
scalar-path resolution. These unlinked objects establish neither linked Flash
nor complete call-chain stack, hardware latency or CPU throughput. Both probes
have no allocation references. Maintained no-heap runs execute full workflows;
raw measurement commands, source hashes and .su files are retained separately
from the checkout.
