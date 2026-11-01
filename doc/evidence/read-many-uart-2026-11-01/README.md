# ReadMany UART qualification, 2026-11-01

Physical NUCLEO-H7S3L8, ST-LINK `002A001F3033510135393935`, COM6,
115200 baud, 8N1. Tested clean source commit
`549d1a1837de8a49cf352caf4fec98665d62e8f5`, Qt 6.10.1 console client and
CubeIDE GCC 14.3 firmware. The optional board profile preserves the legacy
default; it adds 32 readable scalar Field IDs across catalogs and one separate
Unavailable scalar.

The guarded runner backed up all 65,536 bytes of internal Flash, programmed the
test image, ran the real serial client, restored the original image, and compared
a separate complete readback. Backup and restored readback both have SHA-256
`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`.
The original firmware is running again. The backup and binaries remain external
to Git; [qualification.json](qualification.json) records their receipt identities.

## One observed exchange per batch size

| Fields | End-to-end time | Request payload | Reply payload |
| --- | ---: | ---: | ---: |
| 8 | 29 ms | 34 B | 96 B |
| 12 | 34 ms | 50 B | 143 B |
| 16 | 39 ms | 66 B | 190 B |
| 32 | 61 ms | 130 B | 384 B |

Thirty-two individual FieldReads took 540 ms in the same run. These are single
observations, not averages or worst-case bounds. Payload sizes exclude two
32-byte TEQ1 headers and COBS/CRC framing. Every value was compared both with the
deterministic fixture's exact wire bits and an individually read owned baseline;
U64 and signed enum bits never pass through floating point.

The run also confirmed a mixed batch with two successes and one Unavailable,
timeout before handoff without recovery, timeout after a physical request with
recovery, one completion per accepted request, and rejection of a deliberately
withheld late reply. Local TX was released and a complete same-peer Resource
bootstrap established session 2 after session 1. Only then was quiescence
confirmed and fresh admission allowed; the old binding was refused. Exactly
39 Encoded requests were sent, including 32 individual reads and seven batches.

This exercises controlled host timeout and close/reopen of the serial port. It
does not simulate a physical unplug, a reset mid-DMA, or sustained DMA/cache load.
Those remain separate qualification work. Batch reads are sequential samples,
not an atomic measurement snapshot.

## Offline reproduction and integrity checks

[report.json](report.json) retains the canonical response bodies, hashes and
lifecycle results. Validate those records without accessing a board:

```sh
python3 -B tests/nucleo_h7s3l8/VerifyReadManyEvidence.py \
  --report doc/evidence/read-many-uart-2026-11-01/report.json --port COM6 --self-test
```

This validates the recorded report and negative controls; it cannot repeat the
physical operation or independently reread Flash. The external complete runner
bundle also passes `--receipt`, which checks the command logs, frozen executable,
COBS identity, source hashes, backup and restored readback.

Fresh same-toolchain firmware comparison, including the expanded Model and
discovery support:

| Resource | Legacy profile | ReadMany profile | Difference |
| --- | ---: | ---: | ---: |
| Full Flash image, including data load | 22,252 B | 28,784 B | +6,532 B |
| Code `.text` | 18,996 B | 22,476 B | +3,480 B |
| Read-only data | 2,476 B | 5,528 B | +3,052 B |
| AXI RAM reserved extent | 16,032 B | 16,192 B | +160 B |
| Fixed DTCM stack reservation | 8,192 B | 8,192 B | 0 B |

Both images have no allocator symbols. The largest new compiler-reported local
frame is `ServerLoop::step` at 1,024 B; this is not the maximum nested or interrupt
stack requirement. These are complete fixture image differences, not the isolated
cost of ReadMany in another application.
