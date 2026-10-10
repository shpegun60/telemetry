# Codec decode convenience qualification

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[User contract](../../../doc/user/CodecAndWorkspace.md) ·
[Runnable example](../../../examples/user_guide/Codec.cpp) ·
[Suite index](../README.md)

The 2026-10-10/11 capture adds two standalone forms without changing endpoint
dispatch or the existing explicit-Lease decoder:

```cpp
Config config;
auto status = telemetry::decode(bytes, config);

auto result = telemetry::decode<Config>(bytes, workspace);
if (result)
    use(result.value());
```

`Convenience.cpp` executes **2,663 computed checks** on Qt GCC 13.1 and Linux
Clang 18.1.3 with ASan/UBSan. It checks independent little-endian goldens,
nested arrays, late invalid bools, unchanged destination bytes on refusal,
short/long inputs, complete/partial native-object overlap, alignment, nested
LIFO reservations, failed reservations, DMI bypass and 1/4 KiB objects.
Host allocation operators fail immediately. Copy elision that is optional in
C++ is disabled; the immobile result still constructs by guaranteed prvalue
elision. Ten compile-fail cases require mutable-output or deleted-access/
copy/move diagnostics. The original codec, no-heap and ServiceResult probes
and their two compile-fail cases remain in the runner.

```sh
python tests/structured/codec/run.py --cxx g++ --build-dir /tmp/telemetry-codec
python tests/structured/codec/run.py --cxx clang++-18 --sanitize --build-dir /tmp/telemetry-codec-san
python tests/structured/codec/run.py --arm --cxx arm-none-eabi-g++ --build-dir /tmp/telemetry-codec-arm
python tests/structured/codec/endian.py --build-dir /tmp/telemetry-endian
```

The existing CI codec steps run these added controls. The endian runner also
executes `Convenience.cpp` on native little-endian Linux and real big-endian
s390x through QEMU: **12 executions and five byte-identical Descriptor/Values
files** passed. The standalone example runs 72 checks; all nine user-guide
programs pass GCC, GCC null-checks and Clang sanitizers (363 counted conditions
per mode). Their 18 Cortex-M7 O2/Os builds link without device execution.

## Large-object ARM observations

CubeIDE GCC 14.3.1 compiled `ArmConvenience.cpp` at O2/Os/Og. Both 1 KiB and
4 KiB DTOs have the same individual wrapper frames:

| Named wrapper | O2 frame, B | Os frame, B | Og frame, B |
| --- | ---: | ---: | ---: |
| Decode into existing object | 8 | 8 | 24 |
| Explicit Lease control, decode/use/release | 56 | 72 | 56 |
| Scoped result, decode/use/release | 64 | 48 | 48 |
| Place result into caller-provided final storage | 40 | 32 | 24 |

Outlined decode/construction helpers have their own frames, all checked against
256 B. These are individual `.su` records, **not full call-chain stack peaks or
device cycles**. The table does not establish that the scoped wrapper is
universally faster than an explicit Lease. There is no response-sized hidden
stack object, and the inspected objects have no allocator references.

Public checked decoding must not replace `detail::decodeLocalEndpoint` or
`detail::decodeEndpoint` inside prevalidated Field/Command/Service thunks.
Those callers already establish lengths and scratch disjointness. An isolated
same-ABI ARM probe found O2 local wrapper text/frame of 16/0 B for the current
helper versus 36/8 B for public decoding; Workspace wrappers were 112/12 B
versus 134/16 B. These are fixture observations, not total library Flash cost.
Keep the private kernels and expose the new forms at standalone user boundaries.

The [retained firmware relation](../../resources/evidence/README.md) rebuilt
all twelve existing images from both changed public headers. All raw `.bin`
bytes remain identical; this qualifies those existing fixture paths, not every
new overload. No physical board was accessed. Starting source revision was
`6d6d3bf415e0c7e1ebb5f3c91c6521d38bfc5bb0` with local edits; capture logs and
objects are external under `telemetry-validation/20261010-owning-pointer`.
