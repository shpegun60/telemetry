# Native TCP stream host checks

The real Resource and Encoded dispatchers operate on an actual native Model and
Descriptor. Tests exercise every split point of headers/bodies, concatenated
frames and consumed-prefix ownership, known-op reply framing, short errors,
malformed/oversized frames, monotonic request IDs, session changes, reply capacity
before mutation, reentrancy and closure during native callbacks. Descriptor
discovery uses a decoy file at index0 and the descriptor at index1; downloaded
bytes must equal the real packed Descriptor. A separate allocation-trap build
executes the same cases. No socket, STM32 or network device is opened.

```sh
python3 -B tests/tcp_stream/run.py --build-dir /external/tcp-gcc --cxx g++ --arm-cxx arm-none-eabi-g++
python3 -B tests/tcp_stream/run.py --build-dir /external/tcp-clang --cxx clang++-18 --sanitize
```

Use fresh dedicated external output directories. The runner records input/tool/
binary hashes, commands, logs and counts; ASan/UBSan fail on any detected issue.
It separately installs `tcp_server` and `resource_adapter`, then builds/runs an
independent consumer through exported targets. Optional Cortex-M7 compilation
records stack usage and rejects undefined allocation symbols; no ARM code runs.

The [integration contract](../../lib/integration/tcp/README.md) describes framing
and lifetime. Actual TCP adapters and Qt loopback tests are qualified separately.
