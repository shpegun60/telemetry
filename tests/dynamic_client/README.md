# DynamicClient acceptance

Run from the repository root with a fresh external directory:

```sh
python3 -B tests/dynamic_client/run.py --cxx g++ --arm-cxx arm-none-eabi-g++ --build-dir /external/dynamic-gcc
python3 -B tests/dynamic_client/run.py --cxx clang++-18 --sanitize --build-dir /external/dynamic-san
```

The optional Integration component uses the existing Client FIFO and parsed
Runtime schema. `Host.cpp` exercises actual Native Model callbacks through
Encoded, Retrying Encoded and Resource/RPC session-pool backends. It checks all
four operations, immutable request snapshots, capabilities, exact canonical
input, foreign bindings, bounded queue/close behavior, current and stale values,
empty structs versus Void, Accepted versus Executed, malformed replies and
wire-valid invalid Boolean payloads. No alternate wire serializer is introduced.

A second valid Native schema replaces U32 with F32 at the same endpoint and
TypeId and the same five-byte DTO width. Reparse into the same workspace must
cancel the old queued read without any Model invocation. Only quiescence proof
and new admission permit the fresh binding to read the alternate Model float.

Invalidation cases include queued cancellation, reset before handoff, final
execution authorization without acceptance notification, retained checked results,
exact deadline precedence, refused retries, a remote Receiving mailbox after a
non-final RPC WRITE, explicit release/reset plus fresh session admission, stale
reply correlation, and a preauthorized passive READ versus a prohibited new READ.
Recovery and physical quiescence remain separate. Tests assert application-supplied
proofs; they do not infer TX/DMA release from an empty FIFO.

`Gates.cpp` separately instruments gate priority, callback-time quiescence,
checked result type mismatch and old-epoch bindings. The independent
[core-policy tests](../dynamic_client_review/README.md) are invoked by the runner
and retain their own receipt. `NoHeap.cpp` executes the real-backend suite with
replaceable C++ allocation functions trapped and an intentional exit-86 control.
These traps do not claim that libc never allocates internally. `Negative.cpp`
first compiles without a refusal macro, then requires raw, typed, core access,
POD endpoint substitution and binding fabrication attempts to fail compilation.

`Standalone.cpp` checks Runtime+Client-only include closure. Optional Cortex-M7
compilation instantiates submission and handoff methods and rejects allocation
symbol references. Installed-module relocation and forbidden-header tests are
maintained by [the module suite](../modules/README.md). These are host/ASan/UBSan
and offline compiler checks; no board is programmed and no physical STM32, DMA,
RTOS or concurrent runtime metadata guarantee is inferred.

The runner refuses repository output directories and nonempty destinations,
retains timed-out command output, requires runtime check floors, and writes
`summary.json` on command failure. Source hashes cover actual compiler dependency
closure, maintained fixtures, the example and build manifests. Start/end mismatch
fails qualification. Generated binaries and every command log stay in the chosen
external directory. The existing static SDK regression remains an independent
qualification; the default Client admission policy is also covered by the
core-policy suite.
