# ReadMany over UART

This Qt Core/SerialPort console example uses the existing serial COBS transport,
`RuntimeSchema`, `DynamicClient<4, 130, 640>` and the optional Encoded ReadMany
integration. It does not link QML, Quick, an Inspector controller or another FIFO.

It qualifies the opt-in [NUCLEO-H7S3L8 fixture](../nucleo_h7s3l8/README.md),
not arbitrary production device values. The fixture exposes 32 readable scalar
Field IDs across several catalogs, plus an unavailable scalar. Nested members
of aggregate Fields are not counted as separate Field IDs.

```sh
cmake -S examples/read_many_uart -B /tmp/read-many-uart -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64 -DCOBS_ROOT=/path/to/cobs
cmake --build /tmp/read-many-uart
/tmp/read-many-uart/telemetry-read-many-uart \
  --serial /dev/ttyACM0 --baud 115200 --report /tmp/read-many.json
```

Resource LIST/STAT/READ first retrieves the Descriptor and the exact 24-byte
`/telemetry/read-many.bin` TRM1 capability. Only after the full transcript and
fingerprint validation does the client admit that schema/session. Ordinary
Inspector discovery keeps its existing Descriptor-only behavior.

The checker compares 8/12/16/32-field batches with 32 individual FieldReads,
retains canonical reply bytes and hashes, and verifies per-item `Unavailable`.
Every borrowed response is copied inside its callback. Only one request is
pending at a time. Batch values are sequential samples, without atomic snapshot
semantics.

The lifecycle check intentionally withholds a complete real reply, advances its
controlled client clock to the deadline, verifies `Timeout` plus recovery, and
rejects the late reply. It closes the local port, completes a fresh bootstrap
from the same peer and requires a strictly newer advertised session before
confirming quiescence. It then rejects the old binding and performs a fresh
batch. It does not inject a physical cable fault or claim a DMA/cache endurance
test. A board reset during this fixture run fails admission rather than silently
reusing an earlier session.

Build the firmware with `Build.ps1 -ReadMany`. Use the explicit `-ReadManyExe`
mode of [the hardware runner](../../tests/nucleo_h7s3l8/README.md) for controlled
programming and byte-verified restoration. Running this console executable by
itself never programs or resets the board.

Maintained offline checks are in [tests/read_many_serial](../../tests/read_many_serial/README.md).
