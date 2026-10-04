# H7S borrowed/owning result measurements

Authors: Ruslan Kovtun (shpegun60), codexAi. Date: 2026-10-04.

Actual source `7b73fb4c97e48ebb012ea0b6010bc7a4c3c434b3`, CubeIDE ARM GCC 14.3.1, 600 MHz NUCLEO-H7S3L8 with caches enabled. The [receipt](receipt.json) retains `source_dirty=true`; all 135 captured inputs match the sealed Git blobs. The [runner contract](README.md) defines memory placement, warmup, complete FNV consumption, DWT windows and PSP observation.

Each O2/Os image passed 127 correctness conditions with zero failures and compared all 626688 encoded payload bytes. Each contains 294 timing rows and 258 stack rows. All 65536 internal Flash bytes were restored and read back; before/after SHA-256 is `a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.

## Same cached-response profile

Cycles include the full probe, callback, complete-payload FNV consumer and loop/checks. No baseline is subtracted. Each number is the median of seven windows. The receipt also retains alternating and deterministic shuffled profiles.

| Operation | O2 cycles/call | Os cycles/call | O2 / Os observed PSP, B |
| --- | ---: | ---: | ---: |
| field_own_native_4k | 41059.59 | 41061.20 | 8256 / 8248 |
| field_borrow_native_4k | 16435.59 | 16444.08 | 32 / 40 |
| field_own_encoded_4k | 36986.52 | 67790.06 | 88 / 152 |
| field_borrow_encoded_4k | 28750.58 | 55443.66 | 56 / 156 |
| service_own_native_4k | 28753.53 | 28760.05 | 4152 / 4152 |
| service_borrow_native_4k | 16441.53 | 16445.03 | 36 / 48 |
| service_own_encoded_4k | 37005.50 | 67866.58 | 112 / 208 |
| service_borrow_encoded_4k | 24664.39 | 55493.44 | 76 / 212 |
| field_borrow_native_64k | 274222.00 | 273834.50 | 32 / 40 |
| field_own_encoded_64k | 639351.50 | 1123651.88 | 88 / 152 |
| field_borrow_encoded_64k | 425713.50 | 916303.88 | 56 / 156 |
| service_borrow_native_64k | 273236.25 | 273988.88 | 36 / 48 |
| service_own_encoded_64k | 636280.75 | 1126517.38 | 112 / 208 |
| service_borrow_encoded_64k | 425553.25 | 916166.12 | 76 / 212 |

## Interpretation and scope

For native 4 KiB Field, borrowing removes the large result copy: observed PSP falls from 8256/8248 B to 32/40 B. Native Service falls from 4152/4152 B to 36/48 B. Both encoded borrowed families reduce measured cycles and remove Response scratch. Owning encoded already uses caller Workspace, so it never required a 4 KiB response on this PSP. At Os the borrowed encoded probes show **4 B more** observed PSP than owning encoded, despite fewer cycles; this is not a universal stack reduction claim.

Borrowed native PSP stays bounded for 64 KiB. No owning native 64 KiB comparison was instantiated on the 16 KiB stack. The fourteen timed operations use raw `const T&` callbacks. Fallible `BorrowedServiceResult<T>` callbacks are checked outside timing for successful full payload and Busy/no-payload behavior, and on host/ARM software probes for every existing failure status; their cycle cost is not separately measured here.

The six 512 B positive-control stack observations per image report exactly 512 B. Watermarks measure observed writes through nested probe calls, excluding interrupts/UART and caller MSP; untouched reserved stack slots are not established by them. Individual compiler frames and their artifacts remain separate.

The existing owning MCU, Descriptor, Values/resource and Bind/Exchange fixtures were also run at this sealed code commit, in ten additional images, with fresh receipts and full Flash restoration for each family. Original Stage 20 receipts and measurements remain in the [baseline archive](../../../../doc/evidence/pre-borrowed/README.md). The [extension document](../../../../doc/BorrowedNativeValues.md#qualification-results) and [qualification record](../../../../doc/evidence/BorrowedNativeValuesQualification.json) link the separate software and hardware evidence.
