# Compare existing multi-value reads

This host experiment compares eight independent DynamicClient FieldRead requests,
one aggregate FieldRead, and the existing Resource ValuesFile. Each path uses the
real library implementation and checks the same eight returned scalar values.
No ReadMany operation, transport, queue or wire format is added.

Two profiles expose the trade-off:

- `all8`: all eight scalar Fields are wanted.
- `sparse8of32`: eight Fields from a 32-Field Model are wanted. ValuesFile still
  reads all 32, while independent reads and the aggregate return the selected eight.

The aggregate is an alternative Model containing one structured Field. It has a
different Descriptor; it is not also published in the scalar Model, which would
make ValuesFile transmit the same values twice. A coherent aggregate still needs
an application policy for acquiring its members.

ValuesFile is tested from cursor zero, from cursor 24 after its header identity
has been checked, and with a small response capacity that forces whole-token
paging. Continuations must make progress and must not repeat previous getters.

## Run

From the repository root, with the pinned COBS checkout available:

```sh
python3 -B tests/read_comparison/run.py --cxx g++ \
  --cobs-root /path/to/cobs --build-dir /tmp/telemetry-read-comparison-gcc
python3 -B tests/read_comparison/run.py --cxx clang++-18 --sanitize \
  --cobs-root /path/to/cobs --build-dir /tmp/telemetry-read-comparison-clang
```

Use a new output directory outside the checkout for each run. On Windows, use
`python`, a matching CMake/Ninja/compiler toolchain and an external Windows path;
the runner accepts explicit tool paths. It first installs the selected Telemetry
components, then builds this example as a standalone consumer of that package.

The executable accepts `--report <JSON path>`. The runner retains its report,
compiler/build/runtime logs, input and binary hashes, and qualification summary.

## Interpret the report

Getter calls, exchanges and actual TX/RX bytes are deterministic observations.
COBS accounting uses the current serial profile and the real COBS encoder,
including length, CRC, COBS codes and delimiter.

Host execution timings exclude the separate framing-accounting pass. They are
observations for the recorded compiler and machine, not performance thresholds.
Calculated 8N1 airtime includes both directions but excludes device processing,
OS scheduling and link latency. This experiment does not measure physical UART,
DMA, D-cache behaviour or STM32 CPU cycles.

Object sizes and caller-buffer capacities are reported for the measured ABI.
They do not establish linked firmware Flash or worst-case call-chain stack.

See [the comparison contract](../../doc/ReadComparison.md) and the
[maintained runner](../../tests/read_comparison/README.md).
