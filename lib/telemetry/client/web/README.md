`Client.ts` is the single runtime source. `Client.js` and `Client.d.ts` are emitted
with TypeScript 5.9.3, ES2022, strict mode. `build.py` compiles externally and checks
that these files match; `--update` deliberately publishes fresh emitted files.

`DeviceClient` owns request byte snapshots, one FIFO, and one active backend
transaction. It requires generated operation metadata containing byte sizes and
codecs, plus the generated `matchesHeader` identity check. Bindings remain pure
standalone codecs; the CLI copies the runtime only with `--client-runtime`.

The runtime uses Promise, AbortController, queueMicrotask and a monotonic clock.
Each accepted call resolves exactly once. The next backend invocation follows
synchronous Promise result reactions. Async work started inside a `.then` handler
does not hold the queue. All methods execute on one JavaScript owner event loop.
One backend instance and mailbox belong exclusively to one client.

Capacities bound accepted request count and total snapshot bytes; JS allocation
and application codec costs still apply. The snapshot budget does not include
Promise objects, packet framing, decoded results or the caller's retained results.
The request is encoded synchronously before enqueue, and its deadline starts before
that encoding. Results own their payload and decoded generated values.

See the [runnable example](../../../../examples/client_web/README.md) and
[qualification runner](../../../../tests/client_web/run.py).
