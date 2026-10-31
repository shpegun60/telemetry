# Read comparison before ReadMany

The [host experiment](../examples/read_comparison/README.md) measures existing
ways to read several values before choosing a new batch contract. It changes no
production runtime, FIFO, dispatcher, wire format or firmware.

## Existing choices

| Choice | Selected values | Server work | Ownership |
| --- | --- | --- | --- |
| Individual DynamicClient reads | Arbitrary known Field IDs | One getter and exchange per Field | Existing single FIFO; callbacks copy borrowed values |
| Aggregate Field | A structure published by the application | One getter and exchange | One ordinary owned response |
| Resource ValuesFile | All scalar Fields in Descriptor order | Whole Field tokens, optionally paged | Existing Resource integration and caller-owned buffers |

ValuesFile lives in the optional
[Resource adapter](../lib/integration/telemetry/v3/ValuesFile.hpp). Resource core
does not depend on Telemetry. Its stream has a 24-byte identity header followed
by status and fixed-size payload tokens. Whole-token preflight and continuation
must not call a getter again for a token that was already returned.

This stream is not an arbitrary subset operation. The experiment therefore
includes a sparse selection from a larger Model as well as an all-values case.
The aggregate alternative has its own Descriptor and fingerprint. Publishing
that aggregate in the scalar Model as well would unfairly duplicate the values
transmitted by ValuesFile.

Neither sequential Fields nor an aggregate automatically provide a coherent
measurement snapshot. The application must define how it acquires consistent
data across values.

## Fixture traffic

These fixtures request eight 4-byte scalars. The accounting pass records the
following successful cycles through the real protocol and framing code; the
maintained runner checks the counts independently. Discovery and warm header
setup are separate from these cycle totals.

| Profile | Path | Exchanges | Getter calls | Protocol bytes, TX + RX | Framed bytes, TX + RX |
| --- | --- | ---: | ---: | ---: | ---: |
| all8 | Individual | 8 | 8 | 544 | 640 |
| all8 | Aggregate | 1 | 1 | 96 | 108 |
| all8 | Values cold | 1 | 8 | 89 | 101 |
| all8 | Values warm | 1 | 8 | 65 | 77 |
| sparse8of32 | Individual | 8 | 8 | 544 | 640 |
| sparse8of32 | Aggregate | 1 | 1 | 96 | 108 |
| sparse8of32 | Values cold | 1 | 32 | 209 | 221 |
| sparse8of32 | Values warm | 1 | 32 | 185 | 197 |

At a 32-byte Resource data capacity, cold/warm cycles need 3/2 exchanges for
all8 and 7/6 for sparse8of32. Every Field getter still runs exactly once per cycle.
The warm header setup is one separate 49-byte protocol exchange, 61 framed bytes,
and invokes no getter. It is never treated as free first-session discovery.

The report calculates airtime at 1200, 115200 and 4000000 baud from the actual
framed bytes. For example, 640 versus 108 bytes correspond to about 55.56 versus
9.38 ms at 115200 baud, 8N1, before processing or scheduling. Those durations are
arithmetic, not physical UART measurements.

These small scalar fixtures establish a baseline, not a universal recommendation
for large Models or DTOs. The sparse case also makes the extra 24 ValuesFile
getters visible even where its single exchange still saves traffic.

## Measurement boundary

Both profiles use the same immutable native dataset. The dynamic client obtains
types and endpoints from a real Descriptor. Responses pass through the real
Encoded dispatcher or Resource protocol processor. Runtime views validate and
read payloads; callbacks copy the requested scalars before borrowed RX expires.

The report records requested/returned scalar counts, getter calls, exchanges,
protocol bytes, actual framed bytes, capacities and object sizes. ValuesFile has
separate cold, identity-checked warm and small-capacity paging cases. Warm reads
start after the header only after its identity has been verified for this schema.

The real COBS encoder accounts for length, CRC16, COBS codes and delimiter using
the current 2048-byte serial body profile. This accounting pass is excluded from
host timing, so constructing its temporary framing pool cannot bias the measured
client/dispatcher loop. The example reports the timing boundary explicitly.

Host and dispatcher elapsed durations belong to the recorded machine
and compiler. No CI pass/fail decision uses a timing threshold. Calculated 8N1
airtime uses the observed framed bytes in both directions; it excludes CPU,
scheduling, disconnects and physical-driver effects. The existing Qt PTY test
paces only replies, so its elapsed time is not substituted for physical UART RTT.

The steady clock measures elapsed time, including possible scheduling delays;
it does not measure CPU cycles or exclusive processor time.
The instrumented owner loop includes counters, runtime validation and timer
overhead. Buffer poisoning is a correctness-only pass and is excluded from timing.

Object sizes are ABI-specific. Caller buffers and workspace remain explicit;
they are not hidden in a claim about fixed object growth. No linked firmware
Flash, worst-case call-chain stack, STM32 cycles or hardware qualification is
claimed by this host experiment.

## Decision after measurement

Prefer the existing paths when they meet the application's needs. An aggregate
is useful for a stable group; ValuesFile can reduce exchanges when most Fields
are wanted. Independent reads keep arbitrary selection and per-request results.

Consider a separate bounded read-only ReadMany contract only if the sparse case
shows a material unmet need. Before implementation it must specify maximum count
and bytes, capability/admission checks, full output-capacity preflight, per-item
status, duplicate IDs, ordering, failure precedence and schema/session lifetime.
It must state whether consistency is application-defined or explicitly provided.

The current TEQ1 FieldRead requires an empty request payload. It must not be
silently reinterpreted as a list. This experiment does not choose a new wire
version or add Command/Service batching.

The [maintained runner](../tests/read_comparison/README.md) retains reproducible
inputs, logs and reports. Its deterministic correctness checks cover result
equality, counters, framing and paging; host timing remains an observation.
