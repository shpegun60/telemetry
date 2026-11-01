# Qt/QML scalar Watchlist

The optional Watchlist reads up to 32 independent scalar or open-enum Fields
from the existing Qt/QML Workbench. Struct members and array elements are not
independent Field IDs. Batch reads do not promise a simultaneous snapshot.

## Use

1. Start the [Qt/QML application](../examples/inspector_qml/README.md).
   **ReadMany demo** supplies 32 scalar Fields without hardware.
2. For the optional NUCLEO UART ReadMany firmware, enable **Discover ReadMany on
   the next UART connection** before connecting. Discovery obtains the existing
   Descriptor and TRM1 Resource record. Legacy UART discovery remains unchanged
   when this option is off. No firmware is programmed by the GUI.
3. Select a scalar Field in the endpoint tree and press **Add selected**.
   Add Fields from other catalogs in the same way, up to 32 distinct IDs.
4. Choose **ReadMany** and press **Read once** or **Start polling**. The frequency
   control requests 1–10 Hz; achievable throughput depends on UART airtime,
   getter execution and scheduling. Missed ticks are skipped.
5. For a device without TRM1, explicitly choose **Individual FieldRead**. A cycle
   then reads each selected Field sequentially. There is no automatic fan-out
   or substitution after a failed batch.

Only one Watchlist read is pending. The same Workbench-owned DynamicClient and
FIFO execute foreground Field/Command/Service operations and Watchlist reads.
An active foreground operation delays Watchlist submission. There is no second
transport, worker thread or queue. Watchlist reads preserve the foreground
snapshot, editor draft and Service response.

## Lifetime and results

The completion copies formatted scalar values and per-item statuses into
Qt-owned strings before leaving the callback. U64/S64 never pass through a QML
Number. Unknown enum values retain their signed numeric code; NaN and negative
zero display their wire bits. Failed items replace their previous displayed
value with an explicit status.

Stop prevents subsequent reads and lets an already accepted read drain. A
superseded completion is ignored. Editing the list or changing its mode waits
for the entire logical cycle, including an Individual cycle paused behind a
foreground operation, and for any accepted read to drain after Stop. List,
mode and frequency controls remain disabled during that interval; Stop stays
available. Disconnect, schema invalidation or a foreground transport failure
stops polling and notifies existing QML rows that their values are stale.
Logical invalidation does not discard an outstanding read's completion.
The next admission starts an empty list. A timed-out prepared UART
exchange still requires the existing session-recovery procedure; the Watchlist
does not retry it automatically.

TRM1 session and Descriptor fingerprint are checked before admission. When an
old TRM1 session needs recovery, discovery from the same UART peer must prove
a strictly newer remote session before the previous session is retired.

## Optional portable bridge

`Telemetry::read_many_workbench` installs
[`integration/read_many/Workbench.hpp`](../lib/integration/read_many/Workbench.hpp).
It depends only on `workbench` and `read_many_client`. Its `bindReadMany()`,
`readMany()` and `readFieldValue()` functions use the private existing client
without changing Workbench object layout or exposing arbitrary encoded calls.
The ordinary `workbench` component has no ReadMany wire dependency.

The GUI explicitly selects a larger callback budget to own the bounded batch
binding. This does not increase the portable Workbench default. Qt-owned row
storage and display strings belong to the application, not the portable core.

## Qualification scope

Maintained portable bridge checks cover FIFO sharing, preserved drafts,
reentrancy, allocation traps and lifecycle failures. Qt tests exercise actual
production Encoded dispatch, bounded scheduling, owned results, exact scalar
display and the QML controls. Regression tests inspect already-rendered QML
rows after FieldRead, Apply, Command and Service failures, including a lost
Apply readback. They also pause Individual cycles behind foreground work and
verify unchanged IDs, mode, frequency and remaining read order. UART integration uses the established transport
and opt-in TRM1 bootstrap. This feature does not claim an additional physical
board run or sustained DMA/cache stress qualification.
