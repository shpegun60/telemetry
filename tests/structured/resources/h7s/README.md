# H7S values-resource bench and retained evidence

The [receipt](receipt.json) records the 2026-09-27 resource measurement,
with `source_head=c25c6fa00f91a7b6baac9ac23299c2ff29396f62`. It does not record
`source_dirty`; a clean captured source state is not established by that field.
Its original input and image hashes remain unchanged. The measurements in the
[parent README](../README.md) belong to that recorded source snapshot, rather
than the current library paths and namespace.

`verify.py --self-test` checks the retained receipt's internal coverage and
checksum rules offline. It neither rebuilds the captured image nor runs the
current firmware. The bench measures the 73-byte mixed values file, a header
read and the 4126-byte values file containing a 4 KiB native field. It retains
the complete output, status, scratch and callback correctness controls, and
five warmed DWT windows per profile at O2 and Os.

The current `run.py` uses [neutral build support](../../../h7s_support/README.md)
and the current parent fixture. Without `--run`, it only builds both images:

```text
python tests/structured/resources/h7s/run.py --cube COPIED_CUBE_SCAFFOLD --arm-cxx CUBEIDE_ARM_GXX --output FRESH_ARTIFACT_DIRECTORY
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
