# H7S descriptor bench and retained evidence

The [receipt](receipt.json) records the 2026-09-27 descriptor measurement,
with `source_head=45652e7db79debb54d851e6d4c5208bc16379c97`. It does not record
`source_dirty`; a clean captured source state is not established by that field.
Its original input and image hashes remain unchanged. The measurements in the
[parent README](../README.md) belong to that recorded source snapshot, rather
than the current library paths and namespace.

`verify.py --self-test` checks the retained receipt's internal coverage and
checksum rules offline. It neither rebuilds the captured image nor runs the
current firmware. The bench compares streaming and packed reads of the same
1486-byte descriptor: three fixed random-offset chunk sizes and two complete
file transfers, five warmed DWT windows each, at O2 and Os. Its every-offset
byte-equivalence controls remain intact.

The current `run.py` uses [neutral build support](../../../h7s_support/README.md)
and the current parent fixture. Without `--run`, it only builds both images:

```text
python tests/structured/descriptor/h7s/run.py --cube COPIED_CUBE_SCAFFOLD --arm-cxx CUBEIDE_ARM_GXX --output FRESH_ARTIFACT_DIRECTORY
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
