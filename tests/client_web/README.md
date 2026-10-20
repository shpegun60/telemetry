Run `python3 tests/client_web/run.py --build-dir /absolute/external/output --node
/absolute/node --tsc /absolute/typescript/bin/tsc` from the repository root.
The C++ compiler defaults to `g++`; override it with `--cxx`.

The runner strictly compiles the single TypeScript source, checks emitted
JS/declarations, and executes the same queue tests in published JS and compiled TS.
Six line-command scenarios in `traces.json` run against the actual portable C++
queue and both web runtimes, with their normalized completion/start traces compared
exactly. A fake monotonic clock makes enqueue deadlines and cancellation explicit.
The shared trace backend has no physical I/O and explicitly settles on abort.
`Lifetime.mjs` separately exercises transports that ignore AbortSignal: delayed
success and rejection, a stuck exchange, ACK-timeout retry, queued deadline and
cancel while blocked, explicit close/drain confirmation, and late old-session
settlement while a new invocation owns the transport. It also checks asynchronous
Encoded sends and `fromExchange`, backend confirmation refusal/exception/reentry,
and the distinction between logical completion and the `drained` getter.
`Polling.mjs` records deterministic packet timestamps: the first RPC READ follows
the final WRITE ACK immediately; validated Pending/Running replies retain the
default 10 ms poll interval. READ continuations are immediate, ACK-loss retries
retain identical bytes and transaction identity, and cancellation while polling
stops further packets and clears timers. These are fake-clock protocol checks,
not physical transport latency measurements.
Checked Encoded replies also complete before an outstanding asynchronous send
releases I/O: crossing the original deadline or a later send rejection preserves
the known result, while the next queued transport invocation remains blocked.

Generated bindings are also compiled and executed against persistent native C++
Encoded and Resource/RPC subprocesses. These cover all four operations, read-only
refusal, endpoint Busy, Accepted/Executed distinction, own snapshot bytes, large
responses, zero-byte responses, and a lost final WRITE ACK followed by a byte-identical
retry of the same active transaction. The runnable TypeScript example is compiled
and invoked with each runtime.

`summary.json` retains exact commands, counts, selected tool hashes and source
hashes before/after execution. A receipt passes only if those source inputs remain
unchanged. The checks establish host behavior and wire interoperability; they do
not establish physical device completion or durable writes.

`Retrying.mjs` runs the separate opt-in `RetryingEncodedBackend` with a controlled
clock. It checks the default initial send plus three retries, an explicit zero
retry budget, unchanged request identity and bytes, per-attempt handoff and abort,
physical release before retransmission, known late replies, overall enqueue
deadlines, cancellation/close, stale or malformed replies, and explicit old-session
recovery. Delayed old-session exchanges are rejected by their carrier identity
before malformed or detached reply bytes can affect a newer request. Each
attempt receives a private copy, so port mutation cannot change
later retries. Abort-aware `fromExchange` releases a missing response on the attempt
timeout; an exchange ignoring abort remains physically owned until it settles or
the application truthfully confirms closure. No new request overlaps that lease.
Native Encoded bridge scenarios drop either the first request or first reply for
all four operation families, then verify the identical retransmission and endpoint
result in published JS and compiled TS. Retrying a lost reply may execute the
endpoint again; these tests do not claim deduplication or physical completion.
