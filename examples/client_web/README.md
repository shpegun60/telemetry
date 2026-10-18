The asynchronous client accepts multiple logical requests and runs one transaction
at a time on one mailbox/connection. `Promise.all` preserves the per-call results.

Generate the native test device SDK and optional runtime into an external example
directory, copy `client.ts` beside `generated/`, then compile with TypeScript
`--strict --target ES2022 --module ES2022`. The retained runner below performs and
executes this qualification with both emitted JS and compiled TypeScript:

```sh
python3 tests/client_web/run.py --build-dir /absolute/external/web-client \
  --node /absolute/node --tsc /absolute/typescript/bin/tsc
```

Generation is explicit; the standalone device codec keeps no client imports:

```sh
python3 -m tools.telemetry_codegen generate descriptor.bin --targets js,ts \
  --client-runtime --output /absolute/external/generated
```

`rpcClient()` accepts a complete Resource packet exchange callback. Its returned
Promise means a complete response packet, with framing and transport admission
owned by the application. `encodedClient()` accepts a complete-frame send callback
and exposes `receive(frame)`. Callbacks receive an AbortSignal; after cancellation,
deadline or session reset a late reply cannot complete another call. Each outgoing
packet is an owned copy, including retries. The transport retains its copy until
the physical transfer releases it.

Normal operational failures resolve `Result`; inspect `status`, `dispatch`,
`endpointStatus`, `succeeded`, and `accepted`. Command `Accepted` reports application
admission. `Executed` and `Applied` are endpoint-reported outcomes, with no promise
of physical completion or durable storage. Queue overflow and request-byte overflow
resolve explicit refusals. The limit includes the active request; deadlines start
at enqueue, and `request.cancel()` resolves exactly once.

Encoded never retries. RPC retries only the same active Resource packet with the
same transaction ID. An uncertain transaction after its final request handoff
requires recovery even for reads. An abandoned partial RPC request can be replaced
by a later transaction on the same framed session. For uncertain execution, unsent
queued operations are cancelled, and no operation is replayed after
reconnection. `confirmSessionClosed(oldGeneration)` is an application assertion:
close/drain the old connection and release its retained remote pool session before
calling it, then admit a new descriptor header with a strictly newer generation.
Header matching compares the declared 64-byte identity; it does not verify a body
hash or establish peer trust.
