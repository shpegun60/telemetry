# Stage 15 software contract qualification

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This suite fixes the current C++20 public contract and wire v3.0 candidate.
It does not complete the deferred Stage 14 execution on the original H7S.
The [qualification decision](../../../doc/StructuredTelemetryV3FreezeQualification.md)
keeps software contract and hardware qualification as separate statuses.

`Contract.cpp` checks public return types, mixed exact native values, named
local positions, global u32 IDs, shared type identity, nonmoving tables,
empty iteration, static/runtime As access and absence of semantic metadata.
Its **29 counted conditions per optimization** cover local ordered
forEach/visit/container traversal for all three families without reading
values, literal canonical LE bytes, native and encoded operations, checked
conversion, structural mismatch, ID bounds and invalid bool rejection before
the setter. O2/Os/Og therefore execute **87 conditions per host invocation**.
Static assertions and compile rejections are separate from that count.

Ten compile-fail cases require the intended factory/type diagnostic: unit
metadata, wrong Command status, scalar Command request, scalar Service
response, throwing target, pointer request, Command metadata, a different
struct with identical members, structured Scalar fallback and Service
metadata. They preserve rejection contracts; they are not ten new defects.

The readable [contract.json](contract.json) records header widths, LE,
record/type/scalar/status/capability codes, FNV-1a-64 constants, u32 packed
identity, ABI revision, the default 32-byte storage budget, exact dependency
pins and four independent descriptor/values goldens. Generated compile-time
assertions tie its numeric constants to the implementation. Golden hashes
are over decoded bytes, independent of `.hex` whitespace. Existing
descriptor/resource/client suites still compare emitted bytes with their
independent oracles; this suite adds a lock on those expected bytes.

Boost.PFR is checked against canonical tracked Git blob bytes, as described
in [VERSION.md](../../../lib/boost_pfr/VERSION.md), with all 44 headers and
the license. The working tree must match those blobs after LF normalization;
unstaged vendor changes cannot pass by reading an older HEAD. magic_enum's
original tracked header bytes and provenance pin are checked as well.
These checks need a Git checkout; they do not download dependencies.

```sh
python3 tests/structured/freeze/run.py --cxx g++ --build-dir build/freeze-gcc
python3 tests/structured/freeze/run.py --cxx g++ --null-checks --build-dir build/freeze-null
python3 tests/structured/freeze/run.py --cxx clang++-18 --sanitize --build-dir build/freeze-clang
python3 tests/structured/freeze/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/freeze-arm
```

ARM runs compile/link O2/Os/Og, inspect real nm/sections/disassembly, reject
allocation/formatting/Scalar symbols and metadata startup constructors, and
record static individual `.su` frames. They execute **zero ARM conditions**.
The 512-byte frame ceiling is only a bound for this small contract fixture;
the stricter large-object and direct-call gates remain in the qualification
and MCU suites. No new cycle or whole-call-chain stack result is inferred.

`summary.json` records actual compiler/driver flags, executable SHA-256,
input hashes, execution role, counters, sections and individual frames.
Logs and disassembly stay in the selected build directory and CI artifacts.
The full seven-role offline receipt remains in the separate MCU suite.

Locally the contract passed with MinGW 13.1, GCC 13.3 null checks, Clang 18.1
ASan/UBSan, CubeIDE ARM 14.3.1 normal/null and ARM 13.2.1 normal/null. CI runs
the new suite in selected C++20 host modes, Clang sanitizers and both ARM
null-check modes. Publication is qualified by CI for the resulting SHA;
prior green runs do not qualify newly added tests.
