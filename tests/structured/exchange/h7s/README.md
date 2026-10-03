# H7S optional protocol bench and retained evidence

The [receipt](receipt.json) records the 2026-10-03 optional protocol
measurement, with `source_head=cd8b636bc8a518fcc1a3021659f109281d47107e` and
`source_dirty=true`. The captured source hashes identify that local snapshot;
HEAD alone does not identify its complete contents. Its original input and
image hashes remain unchanged. The measurements in the
[parent README](../README.md) belong to that recorded snapshot, rather than a
new measurement after the final telemetry namespace migration.

`verify.py --self-test` checks the retained receipt's internal coverage and
checksum rules offline. It neither rebuilds the captured image nor runs the
current firmware. The bench preserves 4300 MCU correctness conditions and
eight timing profiles: paired encoded-index/Exchange Field write, Command and
Service; a 4 KiB Exchange response; and Bind agreement. Each has five warmed
DWT windows in each O2/Os image. This bench selects the
[protocol example](../../../../examples/structured_protocol/README.md)
explicitly; the core library and generic resources do not select it.

The current `run.py` uses [neutral build support](../../../h7s_support/README.md)
and captures the current parent fixture plus the complete protocol example.
Without `--run`, it only builds both images:

```text
python tests/structured/exchange/h7s/run.py --cube COPIED_CUBE_SCAFFOLD --arm-cxx CUBEIDE_ARM_GXX --output FRESH_ARTIFACT_DIRECTORY
```

Device execution additionally requires explicit `--run --programmer PATH
--serial SERIAL --port PORT`. Before any device access, both complete images
must fit the 64 KiB internal Flash range and match their captured hashes. The
session identifies the requested NUCLEO-H7S3L8, makes a fresh 65536-byte backup,
restores in `finally`, verifies the full read-back hash and leaves the original
image running. Option bytes and external memory are outside the session.

The current bench retains the established GFXMMU Device/no-access MPU region
before enabling caches. No current-source board measurement is established by
the retained receipt or a successful compile. New generated code placement
can change cycle measurements. Fresh results belong in the fresh artifact
directory, with captured LF inputs, actual HEAD/dirty state, compiler identity,
flags, scaffold and image hashes, raw UART rows, maps and stack reports.
