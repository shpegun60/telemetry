# Stable registry roots regression fixture

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[Fixture.hpp](Fixture.hpp) retains positional `RootTypes` assertions for local
Field, Command and Service tables. New-build controls independently require
stable first-occurrence `RegistryRootTypes` in local tables and catalogs, empty
lists, normalized cv/ref identity, distinct same-shape types and 2048 repeated
roots under default compiler depth. Distinct 33/65 marker sequences test block
argument order and normalized duplicates crossing block boundaries.
A RootTypes-only application wrapper checks
composition compatibility without claiming support for a custom Descriptor shape.

[Check.cpp](Check.cpp) checks eight Fields, six Commands and seven Services across
two nonempty groups plus an empty middle group in every family. Three Fields in
the first table share the exact Packet type, including owning and borrowed
getters. Values must still contain eight tokens and 67 bytes. The count cannot
be inferred from a deduplicated type list. Explicit token bytes, 680
cursor/capacity cases, no-fit getter counts, interior-cursor rejection and
chunked reconstruction exercise the complete positional values stream.

Nested Leaf/array dependencies, a distinct SameShape DTO, an enum and Void roots
have fixed expected TypeIds. Query and Reply first occur in a Service
Request/Response pair. The local serviceB positional and unique-list assertions
specifically detect grouping all Requests before all Responses: their correct
first-use sequence is Packet, SameShape, Query, Reply.

The program writes three exact artifacts: `descriptor.bin`, `values.bin` and
`type-ids.bin`. The last contains canonical u32 identities for every endpoint and
the shared registry. No native pointer addresses, C++ padding or ELF symbol
names enter these files. Before/after comparison requires identical fixture
source hashes; only the new-alias assertion macro differs. ELF SHA is retained
for input evidence but is not expected to stay unchanged.

Run from the checkout, using a pristine external archive for the baseline:

```sh
python3 tests/scalability/root_types/run.py --cxx g++ \
  --source-root /external/root-dedup/baseline \
  --build-dir /external/root-dedup/before-gcc

python3 tests/scalability/root_types/run.py --cxx g++ --expect-new \
  --before-dir /external/root-dedup/before-gcc \
  --build-dir /external/root-dedup/after-gcc

python3 tests/scalability/root_types/run.py --cxx clang++-18 --expect-new --sanitize \
  --before-dir /external/root-dedup/before-gcc \
  --build-dir /external/root-dedup/after-clang-sanitized
```

`--optimization O2/Os/Og` selects host optimization. Each command has a 180-second
timeout. An output directory must be fresh and outside the checkout and source
archive. A compilation, assertion, coverage, source-coherence or byte mismatch
fails the runner. Generated binaries and logs remain in that external directory.
This fixture establishes correctness and canonical parity; it does not measure
compile scaling, Cortex-M cycles, or operating-system transport behavior.

The 2026-10-05 local comparison used the raw Git-blob archive of `34cf159` and
the changed working tree, with identical final Fixture.hpp and Check.cpp inputs.
GCC 13.3.0 and Clang 18.1.3 each passed baseline/current builds; current Clang
also passed ASan+UBSan. Every execution counted 4995 assertions, including the
680 cursor/capacity cases. All three artifact files matched exactly:

| Artifact | Bytes | Meaning |
| --- | ---: | --- |
| descriptor.bin | 1566 | Complete canonical descriptor, including TypeIds and names |
| values.bin | 67 | Eight current Field tokens, header and descriptor fingerprint |
| type-ids.bin | 236 | Registry and every endpoint's canonical type identity |

The descriptor fingerprint was `724a6d8d17a90361` in all final runs. Receipts,
exact compiler commands, hashes and generated outputs remain under
`C:/Users/admin/Documents/telemetry-validation/20261005-root-dedup/` in the
`final-before-gcc`, `final-after-gcc`, `final-before-clang`, `final-after-clang`
and `final-after-clang-sanitized` directories. Earlier runs remain intact.
An initial driver-only coverage threshold of 5000 incorrectly rejected the
actual successful counter of 4995; that harness result was excluded, and the
final driver requires the exact counted population instead.
