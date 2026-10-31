# Read comparison qualification

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

The maintained runner builds and installs selected Telemetry modules, then builds
the [standalone example](../../examples/read_comparison/) against that installed
package. It executes the actual DynamicClient/Encoded dispatcher and Resource
ValuesFile read paths. No Qt, network connection or board is required.

Run from the repository root with an external COBS checkout and a fresh output
directory. The runner refuses a nonempty directory and never removes files:

```sh
python3 -B tests/read_comparison/run.py --build-dir /tmp/read-comparison-gcc \
  --cobs-root /path/to/cobs --cxx /usr/bin/g++ \
  --cmake /usr/bin/cmake --ninja /usr/bin/ninja

python3 -B tests/read_comparison/run.py --build-dir /tmp/read-comparison-sanitized \
  --cobs-root /path/to/cobs --cxx /usr/bin/clang++-18 --sanitize \
  --cmake /usr/bin/cmake --ninja /usr/bin/ninja
```

Windows accepts native paths for the same arguments, including paths with
spaces. Put the selected compiler's runtime directory first in `PATH`. The
runner also places its `--cxx` directory first for its child processes.

`summary.json` retains the tool versions and hashes, Git identities, input hashes
before and after, exact configure/build/runtime commands and log hashes, installed
package compile/link evidence, executable hash and compiler `.su` file hashes.
`comparison.json` is the example's deterministic accounting and host timing
report. GCC and Clang profiles have separate directories; the sanitized profile
enables AddressSanitizer, UndefinedBehaviorSanitizer and leak checking.

The framing helper uses the existing COBS encoder with the shared Inspector
serial profile. Its observed size includes the length prefix, CRC16, COBS code
bytes and delimiter. Empty, maximum-size and oversized inputs and actual
encoder/decoder round trips are checked. No COBS serialization is reimplemented.

The runner checks values, getter and exchange counts, traffic, warm cursors and
paging. It applies no elapsed-time performance threshold. Host timing excludes
the separate framing accounting pass and synthetic buffer poisoning. Reported
UART airtime is rounded-up 8N1 arithmetic for the counted bytes, without driver,
processing or scheduling delays; it is not measured link latency. None of these
measurements establishes
physical UART throughput, STM32 stack use, atomic snapshots or a new batch wire
operation. The existing `qml-codegen` CI job runs both compiler profiles after
checking out its pinned COBS version and retains both in `generated-qml-sdk`.
