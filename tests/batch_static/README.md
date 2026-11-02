# Typed static Mixed Batch qualification

`Example.cpp` is the short successful user scenario: build typed items, create an
explicit Store, bind the peer's TBM1 once, submit and check each result. Write,
Read, Command and Service use the existing client/FIFO. The connection fixture
supplies a real native Model and Encoded dispatcher; the user-facing code never
constructs payload bytes. The write/read pair checks device normalization rather
than echoing the submitted DTO.

`Standalone.cpp` is the separate minimal installed-client isolation probe. It
uses all four item kinds and the unchanged default Callback64, then closes before
activation and checks cancellation, absence of invented per-item results, and
Store release. It does not claim successful endpoint execution. Its SDK closure
must not import Native Model, RuntimeSchema, DynamicClient, Inspector or Qt.

```sh
python3 -B tests/batch_static/run.py --cxx g++ \
  --build-dir /absolute/external/static-batch-gcc
python3 -B tests/batch_static/run.py --cxx clang++-18 --sanitize \
  --build-dir /absolute/external/static-batch-asan
```

The runner exports the actual Model Descriptor, uses the unchanged C++ generator,
installs selected SDK components, relocates each prefix, and compiles isolated
consumers. Source, generated, tool and executable hashes, commands and logs are
retained. Output must be a fresh external directory; nothing is deleted.

`Host.cpp` covers generated and independent handwritten DTO codecs, all four
operations, nonzero catalog IDs shared across categories, repeated write/read
IDs, exact order and a native execution trace, per-item application/dispatch
errors, Accepted versus Executed, Void versus empty Struct, arrays, unknown enum,
exact U64/S64, NaN payload and signed zero. It checks one FIFO alongside ordinary
typed reads; QueueFull/PayloadFull before encoding; Store Busy/reuse; first and
later callback moves, moved-from and final callable destructors; reentrant
encoding/decoding; stale sessions; malformed final items; full suppression of
typed values when a later item fails; and pure recovery/Closed status gates.

Fake-clock tests distinguish queued, offered and prepared cancellation/timeout,
including exact deadline boundaries and a write executed with a missing reply.
No automatic replay is allowed. An unknown outer outcome produces no invented
per-item success. Known wire statuses remain separate from typed value validity.

Repeated-item profiles execute 8/32 scalar reads and 32 DTO services with default
Callback64. They report object sizes plus encoded/request/response budgets for
the current compiler/ABI. The reported illustrative client has two slots and
payload capacity for one complete request; concurrent maximum requests require
an appropriate shared FIFO payload budget. Store sizes exclude the client,
backend, transport buffers, recipe/argument objects and stack frames.

`NoHeap.cpp` repeats the host suite under allocation traps, with six separate
positive controls that must exit 86. `Negative.cpp` verifies diagnostics for
temporary arguments, implicit value conversions, wrong endpoint category,
selection bounds, throwing callbacks/ownership, insufficient callback capacity,
oversized DTO contracts, private state, Store copying and Void value access.

```sh
python3 -B tests/batch_static/run.py --arm \
  --cxx /path/to/arm-none-eabi-g++ --producer-cxx g++ \
  --build-dir /absolute/external/static-batch-arm
```

ARM qualification links selected generated/handwritten operation roots at `-Os`
and `-O2`. The entry point receives caller-owned admitted clients and concrete
backends. Stores use file-scope `constinit`; their trivial destruction is checked.
Heap, runtime guard and atexit symbols are forbidden in this selected root. This
does not measure backend construction/vtables or a complete firmware image.
Section sizes and compiler `.su` frames are recorded, without claiming physical
peak stack, MCU execution, UART/DMA qualification, rollback or exactly-once work.
