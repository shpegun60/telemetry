# Protocol-free encoded dispatcher checks

Run from the repository root, with all output outside it:

```sh
python3 -B tests/encoded_dispatch/run.py --build-dir /tmp/telemetry-encoded-dispatch
python3 -B tests/encoded_dispatch/run.py --build-dir /tmp/telemetry-encoded-dispatch-san \
  --cxx clang++ --sanitize
```

The core fixture includes only `telemetry/Telemetry.hpp` and links the existing
`Adapter.cpp` and `StructuredAbi.cpp`. Its compiler dependency file must contain
no Resource or Client header. The tests force caller-owned scratch with
`TELEMETRY_STRUCTURED_LOCAL_BYTES=0`, disable delegate heap fallback, and compile
without exceptions or RTTI.

The checks exercise all four operations and their separate application status
families, successful bytes, Empty/Void calls, undefined operations and missing
endpoints, malformed bool/lengths, insufficient output/scratch, readonly-write
error precedence, native Service in-place/partial-overlap support, and scratch
overlap refusal. Callback counts and untouched output prove preflight errors
do not invoke endpoints. Invalid callback status is checked separately after
invocation, so `InternalError` never implies that an effect did not occur.

Each external `summary.json` records compiler, exact commands, source HEAD and
dirty state, source hashes before/after, dependency isolation, checks and binary
hash. These are host checks; they do not establish board or transport behavior.
