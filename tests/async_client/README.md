# Portable asynchronous client qualification

```sh
python3 tests/async_client/run.py --build-dir /tmp/telemetry-async --arm-cxx arm-none-eabi-g++
python3 tests/async_client/run.py --build-dir /tmp/telemetry-async-asan --cxx clang++-18 --sanitize
python3 tests/async_client/run.py --build-dir /tmp/telemetry-async-tsan --cxx clang++-18 --thread-sanitize
python3 tests/async_client/Encoded_run.py --build-dir /tmp/telemetry-encoded --no-heap
```

Core checks exercise bounded FIFO, immutable snapshots, overflow, callbacks,
reentrancy, stale handles, queued/active cancellation, enqueue deadlines and
clock wrap. Four producers enqueue/cancel against one owner; a delayed private
producer reservation overlaps close and explicit drain. `NoHeap.cpp` repeats
owner lifecycle checks with allocation trapped.

The close overlap also checks that a producer wakes a notification-driven owner
after releasing its final private reservation, both when publication is refused
as Closed and when encoding fails. Refused work never invokes a callback.

The dedicated `MPSC ThreadSanitizer (Linux)` [CI job](../../.github/workflows/ci.yml)
uses Ubuntu 24.04 and Clang 18. The existing `--thread-sanitize` mode runs the
core lifecycle/MPSC executable and compile-time contracts; it does not combine
TSan with address/undefined sanitizers or run the backend, noheap or ARM gates.
CI requires the positive core check count and instrumented TSan symbols, and
retains compiler commands, runtime output and source hashes. A runtime startup
failure fails the job; the job does not change ASLR, suppress reports or turn
failures into skips. TSan covers the executed host interleavings, not every
possible schedule or a physical board's ISR/RTOS behavior.

Real backend checks call actual Model Encoded operations and an actual
RpcSessionPool, including all four operations, Accepted, partial requests,
withheld final ACK, same-transaction retries, uncertain read/mutation recovery,
and admission/size/status validation before effects. The focused Encoded runner
retains an independent wire golden and persistent bridge exchanges.

The runner generates fresh SDKs from an actual native Descriptor, executes the
unchanged [bare-metal loop](../../examples/async_client/README.md) with a host
port and optionally compiles it for Cortex-M7. The ARM dependency, undefined
symbol and 512-byte per-frame stack gates exclude platform/heap regressions.
The [shared trace driver](Trace.cpp) is exercised by the
[web runner](../client_web/README.md) against identical input actions.

All generated files, executables, logs and `summary.json` stay in the external
build directory. No board is flashed by these runners.
