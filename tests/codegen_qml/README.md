# Generated Qt/QML qualification

The runner generates an SDK from the native fixture descriptor, compiles its Qt
module, imports and instantiates it in an actual `QQmlEngine`, and exercises both
native Encoded calls and the real Resource/RPC mailbox. It runs offscreen with a
software Qt Quick backend. No hardware or network connection is used.

From the repository root on Windows:

```powershell
python tests/codegen_qml/run.py `
  --build-dir C:/Users/admin/.codex/artifacts/telemetry-qml-2026-10-18/windows `
  --qt-prefix C:/Qt/6.10.1/mingw_64 `
  --cxx C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  --cmake C:/Qt/Tools/CMake_64/bin/cmake.exe `
  --ninja C:/Qt/Tools/Ninja/ninja.exe
```

Use the compiler distributed for that Qt build. The runner puts its directory
before other MinGW installations in `PATH`. On Linux, pass the Qt CMake prefix
and explicit compiler/CMake/Ninja paths. Qt 6.4 or newer with Core, Gui, Qml,
Quick, and Test is required. Example validation additionally uses QuickControls2.

The checks cover asynchronous tokens, direct and RPC requests for all four
operations, exact native payloads, accepted work, failed statuses before payload
decoding, valid empty aggregates, malformed responses, cached property NOTIFY,
getter reads without I/O, canonical 64-bit decimal strings, nonfinite floating
point values, negative zero, actual QML object conversion/refusal, session
admission/reset, stale and duplicate completion, endpoint Busy, and bounded
timeouts. RPC storage forces request/result fragmentation and wrong-generation
replies are rejected. The admission check compares the exact declared 64-byte
header; it does not authenticate a peer or hash a remote descriptor body.

Known-result regressions reset or readmit the session between checked completion
and queued public delivery. Executed and Accepted results retain their status
and decoded values, including through both generated adapters. Separate cache
checks prevent old reads from populating a new session and old writes from
invalidating its confirmed value, including during nested event processing.

The queued executable additionally injects RetryingEncodedBackend into the same
generated adapter. A dropped TEQ1 reply causes byte-identical same-ID retransmission
and two actual native command calls, with one public completion. The checks cover
all four operations, Accepted versus Executed, QueueFull, normal cache behavior,
fresh retry handoff permission, and reset/recovery without automatic replay.

`summary.json` records commands, return codes, source hashes and timestamps;
individual logs contain build and runtime evidence. Outputs must be outside the
repository. A passing host run does not qualify a physical device or transport.
