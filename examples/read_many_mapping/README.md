# Experimental ReadMany mapping comparison

This is a bounded host prototype for the [ReadMany contract](../../doc/ReadMany.md),
not a supported device/client API. It compares compact experimental frames with
an ordinary fixed-size application Service using the same Field executor.
Neither DynamicClient nor Workbench sends ReadMany in this example.

The [mapping decision](../../doc/ReadManyMapping.md) describes the candidate layout,
capability and remaining integration work. Current TEQ1, RPC, TCP routing and Nucleo
firmware do not accept the experimental frames. The ordinary Service uses existing
TEQ1 Service dispatch, with its existing potentially-mutating timeout semantics.

## Build and run

Use the maintained installed-package checks from the repository root:

```sh
python3 -B tests/read_many_mapping/run.py --build-dir /tmp/telemetry-read-many-gcc \
  --cobs-root /path/to/cobs --cxx g++ --cmake cmake --ninja ninja
python3 -B tests/read_many_mapping/run.py --build-dir /tmp/telemetry-read-many-clang \
  --cobs-root /path/to/cobs --cxx clang++-18 --sanitize
```

Outputs must be fresh directories outside the source trees. The runner installs
only `core`, `runtime`, `encoded_server` and `resource_protocol`, then builds this
project against that prefix. For direct CMake use, set `Telemetry_DIR` to its
`lib/cmake/Telemetry` directory and set `COBS_ROOT` explicitly. The external COBS
revision used in CI is `2e0abf260848fcb74e4a57b37532188047759643`.

`read-many-mapping --report mapping.json` executes the controls and ten profiles:
1/8/12/16/32 selected U32 Fields from two catalogs, with zero or 128 iterations
of deterministic getter work. Values reuse the immutable PR #13 dataset; the
eight-field selection is the same linear 0/4/8/.../28 subset. The catalog layout
differs deliberately to exercise the upper 16 bits of packed Field IDs.

`read-many-noheap` checks actual parsing, execution, retained reply validation and
ordinary Service dispatch with C++ allocation and C allocation-call traps armed.
The runner also invokes six deliberate controls; each must exit with code 86.

## Measurement boundary

Actual COBS encoding runs separately from timing. Durations are instrumented
synchronous host elapsed time, including validation/counters/timer overhead;
they are not CPU cycles, physical UART RTT or production DeviceClient latency.
The expensive getter is synthetic work, not a measurement of STM32 peripherals.

The report records explicit retained-scalar, Service packing and active-ID staging
copies. It does not count all compiler temporaries, Codec writes or total memory
traffic. ABI-specific object sizes and caller buffers are reported separately;
the Context already contains metadata, Batch and buffers, so they are not additive.
Captured requests and returned reply views are additional caller objects.

Capability accounting is one Resource READ of a known file ID: 61 framed bytes.
LIST/path resolution, Descriptor discovery and session establishment are excluded.
The sample path `/prototype/read-many.cap` and file ID are application choices.

All Model/schema/native-owner/control storage must remain alive and disjoint from
request, response and Workspace buffers. The admitted capability must describe
the supplied Model. The dispatcher does not recompute its Descriptor fingerprint
for each request. Returned spans borrow the unchanged reply; retained values must
be copied before reuse. Workspace reset invalidates captured schema access.

See the [test runner](../../tests/read_many_mapping/README.md) for offline ARM
selected-root sizes, individual stack frames and qualification evidence.
