# Sustained ReadMany UART qualification, 2026-11-02

Physical NUCLEO-H7S3L8, COM6 at 115200 baud, 8N1. Tested clean source
`810183404032a72ab9b0a7eadec753aa7e887241`. The firmware/library compile scope is unchanged from `492ef45afeccfcf37d3a6425fc29a647ad51964d`;
the new work is the bounded sustained-load checker and guarded evidence lifecycle.

The runner completed a requested 1800-second maximum-speed
sequential application load in 1800039ms, validating 43932
exchanges and 741356 OK / 5488 Unavailable item results.
Every accepted exchange had one callback and authorized transport handoff.
The existing deadline, late-reply rejection and same-peer bootstrap lifecycle
then passed. No automatic retries or atomic-snapshot promise were added.

| Fields | Exchanges | Min ms | Mean ms | Max ms | Unavailable items |
| --- | ---: | ---: | ---: | ---: | ---: |
| 8 | 10983 | 23.874 | 29.188 | 34.947 | 1372 |
| 12 | 10983 | 30.597 | 34.082 | 40.586 | 1372 |
| 16 | 10983 | 35.495 | 39.689 | 42.827 | 1372 |
| 32 | 10983 | 58.014 | 60.779 | 65.230 | 1372 |

Full exchange latency is measured by the host application; these are observations,
not universal bounds. Byte totals in [report.json](report.json) are payload bytes,
excluding TEQ headers, COBS/CRC and UART bits. The report retains four latest
validated samples and bounded aggregates. The reply-payload transcript digest is
retained but its full historical content is not independently rehashed.

The firmware profile enables I/D caches and uses the pinned external UART DMA
cache maintenance path. This run verifies sustained end-to-end exact responses
for a mostly constant scalar fixture with changing IDs/order/layouts. It does not
establish universal cache coherency, collect SWD UART/DMA counters, measure CPU
utilization/runtime stack, or inject physical cable removal/reset mid-DMA.

All 65,536 original internal Flash bytes were restored and compared directly with a
fresh complete readback; both SHA256 values are `a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`. Flash backups,
readbacks, firmware/executable binaries, raw logs and private path inventories
remain outside Git. This preserves Flash bytes, not the earlier volatile state.

[qualification.json](qualification.json) records artifact/restore identities;
[sources.json](sources.json) maps tested Telemetry input paths to immutable Git
blob IDs and CRLF-to-LF normalized SHA256 identities. Match each Git blob using
`git cat-file blob <git_blob_oid>`, normalize CRLF to LF and compare SHA256. This
binds retained source inventories/artifacts, not reproducible-build equivalence.
[manifest.json](manifest.json) hashes these public files.

Offline report verification without hardware:

```sh
python3 -B tests/nucleo_h7s3l8/VerifyReadManyEvidence.py --report doc/evidence/read-many-uart-load-2026-11-02/report.json --port COM6 --minimum-load-seconds 1800
```

The verifier checks retained consistency; it does not repeat a physical run.

## Offline image conversion identity

The frozen ELF SHA-256 is `8be14013c1cfc9056bc65670f4b6e8bec84cdd8fbde26714040c0bad9119c045`. Offline reruns of the
recorded, hash-checked CubeIDE objcopy reproduced both binary outputs byte for
byte. Build.ps1 uses default gap fill; the guarded programming runner explicitly
uses `--gap-fill 0xff`. Build BIN SHA-256 is `87ac134151af1c31aaea0e46c15599898217615948367efef40fdf91f658ebc5`; programmed BIN
SHA-256 is `52f70f021d85cddedc77e3c1b60a9aa9122fb3bdb3542fde86fb886a3bcfc04e`. They differ in 4
padding bytes outside every allocated file-backed ELF section. All section
bytes are identical to the ELF in both outputs. Exact offsets and section hashes
appear in `qualification.json`; the distinct binary hashes are retained.

## Earlier failed attempt retained

An earlier attempt completed 39004 validated exchanges over
1800000 ms, and its CLI reported success. The overall hardware
runner qualification nevertheless **failed**: Windows denied starting the
WindowsApps Python verifier through .NET Process.Start. The original 65,536 Flash
bytes were independently restored and matched.

Its observed maximum exchange latency was 142.181 ms,
compared with 65.230 ms in the later qualified run.
Both observations are retained: host timing varies and the later maximum is not
a universal bound.

That attempt remains failed and its original receipt/report are untouched.
Their SHA-256 identities and restoration summary appear separately in
`qualification.json` under `prior_failed_attempt`. It was not relabelled as a
passed run. This publication describes the later complete successful pipeline.
