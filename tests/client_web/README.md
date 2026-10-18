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
