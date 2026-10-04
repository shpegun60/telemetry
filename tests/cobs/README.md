# Real COBS integration checks

The [working example](../../examples/cobs_integration/README.md) uses actual
COBS endpoints, CRC, fixed blocks and borrowed transport callbacks. No framing
simulation replaces the COBS encoder/decoder. It executes **295 conditions**:
Field read/write/refusal, Command, Service, resource LIST/STAT/READ/partial WRITE,
chunked receive, busy-send ownership and gap recovery through the delimiter.

The runner requires the pinned COBS Git revision
`2e0abf260848fcb74e4a57b37532188047759643` and records actual source/image hashes.
The external checkout is read-only; it is not a telemetry-core dependency.

```sh
python3 tests/cobs/run.py --cobs-root ../cobs --build-dir /tmp/telemetry-cobs
python3 tests/cobs/run.py --cxx clang++-18 --sanitize --cobs-root ../cobs --build-dir /tmp/telemetry-cobs-san
python3 tests/cobs/run.py --arm --cxx arm-none-eabi-g++ --cobs-root ../cobs --build-dir /tmp/telemetry-cobs-arm
python3 tests/cobs/run.py --cxx s390x-linux-gnu-g++ --emulator qemu-s390x --sysroot /usr/s390x-linux-gnu --expect-big-endian --cobs-root ../cobs --build-dir /tmp/telemetry-cobs-big
```

Host/sanitized/big-endian modes execute the example. Cortex-M7 O2/Os/Og modes
compile and link images only; they do not establish cycles, peak stack or board
behavior. CI preserves command logs, image hashes and actual condition counts.

The separate [cross-endian runner](../structured/codec/endian.py) executes four
existing contract programs on little-endian x86-64 and big-endian s390x. It
compares five full Descriptor/Values files byte for byte and asserts native
endianness in the compiler. This exercises the actual fallback codec branch.
