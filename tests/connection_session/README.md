# Optional headless connection Session

This suite consumes an actual `npm pack` tarball installed offline into a fresh
external directory. Runtime tests import only public `telemetry-sdk` package
paths and actual generated SDK modules. The SDK requires no Telemetry UX package,
DOM, Qt, hardware or native UI.

```sh
python3 -B tests/connection_session/run.py --build-dir /absolute/external/session-check \
  --node /path/to/node --tsc /path/to/typescript/lib/tsc.js \
  --npm-cli /path/to/npm/bin/npm-cli.js --cxx g++
```

`Check.mjs` uses the existing SDK `Trace.mjs` fake monotonic clock and delayed
backend/fence patterns. Static and Dynamic exercise admission, exact header
identity, empty aggregate versus Void, codec failures, copied capability discovery,
one FIFO, queued cancellation, pre/post-handoff loss, operation facts, failed or
delayed local closure, remote supersession association, backend confirmation,
late bootstrap and old backend callbacks, exclusive profile ownership, terminal
close, reentrant observers and retained recovery across a replacement owner.
An `async_hooks` boundary sweep proves whether the public request Promise has
resolved before revocation, covering stale value suppression after Dynamic
decoding and before Session projection while retaining checked operation facts.

`Profile.mjs` injects controlled fetch/WebSocket/clock surfaces into the named
reference profile. It checks advertised routes/subprotocol, bounded greetings,
full bootstrap deadlines, aborted fetches, actual socket CLOSED fences, capability
HTTP failures, cleanup and the extracted borrowed-carrier ownership contract.
These checks establish local profile behavior; fake receipts do not establish a
remote server's lease guarantee.

`Integration.mjs` reuses the existing SDK `client_dynamic_web/Bridge.cpp` and
`batch_static/Model.hpp`. Actual Descriptor generation and native Encoded dispatch
exercise all four Session operations, Accepted, endpoint refusal, empty aggregate,
Void and exact 64-bit data through both modes. A concurrent second physical
exchange is rejected by the test peer. A separate acceptance run against the
existing reference HTTP/WS server establishes network and same-peer lease behavior.

`Consumer.ts` strictly compiles public root/subpath imports against installed
declarations, including generated Static inference, Dynamic `unknown`, request
identity/cancellation, the borrowed-only `telemetry-sdk/transports/websocket/carrier`
entry point, and expected type refusals. The prepared package directory
is renamed after installation so it cannot masquerade as the installed consumer.

The runner checks strict single-source Session emission and records
source/tool/tarball/executable/descriptor hashes, exact command
logs, maintained scenario/check counters and `summary.json`. Its output must be
fresh; reusing a nonempty directory invalidates the previous success report and
returns failure without deleting files. A failed or incomplete run cannot retain
`passed: true`. `--sanitize` enables compiler-supported native sanitizers.
