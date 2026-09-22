# Resource validation

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

Run from the repository root with Python 3, a C++20 compiler, and Node 22 for
the browser decoder:

```sh
python tests/resources/run.py --node node --build-dir build/resources
python tests/resources/run.py --cxx clang++-18 --sanitize --node node --build-dir build/resources-san
python tests/resources/run.py --arm --cxx arm-none-eabi-g++ --build-dir build/resources-arm
```

`--objdump` and `--size` select matching ARM tools. For STM32 local builds use
the CubeIDE compiler. Logs, objects, binary fixtures and stack-usage reports
remain in the requested build directory. A host run without `--node` runs the
C++ checks only; CI supplies Node explicitly.

The source style is [.clang-format](.clang-format): four spaces, explicit blocks
and 100 columns. Apply it to resource code, not vendor headers:

```sh
clang-format --style=file:tests/resources/.clang-format -i path/to/source.cpp
```

## Coverage

- CoreCheck: 4162 checks of core/protocol bounds, const providers, borrowed
  lifetimes, capabilities, packet byte order, whole-path LIST paging, repeated
  WRITE delivery and malformed packets. Ok/unfinished callbacks must make
  byte or cursor progress; zero-byte EOF/complete and cursor-only progress work.
- BinaryCheck: 793478 checks of exact scalar patterns, signed minima, U64 max,
  bool/Null, negative zero, subnormals, NaN payloads and infinities. Partial
  writes at every scalar offset, raw strings including NUL/UTF-8, checked u32
  length overflow and 80000 random typed payloads are covered.
- StreamCheck: 4417 checks of record offsets, empty/tiny buffers, atomic
  preflight, output guards, EOF and invalid cursors.
- TelemetryFilesCheck: 22903 checks with an independent binary reader against
  the real metadata. Full-file goldens pin schema, commands and values bytes,
  including fingerprints. Chunk capacities 1/2/3/7/31/63/127/220/256/1024 and
  every record byte offset reconstruct identical metadata. Checks cover
  min/max/default, enum dictionaries, maximum packed ID, empty catalogs,
  reserved fields, labels with quotes/newlines/UTF-8 and enum names with NUL,
  null versus empty parameter labels, flags/fingerprint changes, atomic getter
  counts, changing readings, unavailable zero payloads and protocol READ paging.
- DecoderCheck: Node runs the actual browser ES module against the C++ fixtures.
  Exact BigInt/signed/float values, unknown type-99 records with 17-byte payloads,
  minor/header extensions, 1655 truncation cases, invalid lengths/counts/status,
  nonzero byteOffset views and 6000 deterministic packet mutations are covered.
- DeviceCheck: all three `.bin` paths and the application facade from a consumer
  that includes no telemetry headers. Protocol LIST is also checked.
- NoHeapCheck: C++ new/new[] are rejected during construction and transfers.
  ARM object checks additionally reject malloc/calloc/realloc/operator new
  references from adapter/protocol code.
- Fifteen resource headers compile independently, including internal codec
  headers. Eleven negative compile cases have a successful control. Each of
  three adapters has matching-layout link success and mixed-layout rejection.
- ArmProbe: FileEntry is 16 bytes, its offsets 0/8/12; view is 8 bytes. ReadResult
  and WriteResult are 16 bytes and FileStat 8 bytes. Constant descriptor storage
  and direct known-provider dispatch are verified at both optimizations.

[Golden.hpp](Golden.hpp) specifies the complete bytes for catalog `m`, field
`V` of F32 (limits 0..300, default230), and command `C(P: F32)` with the same
limits. These bytes are not regenerated from the production encoder during tests.

## Integration

The resource runner deliberately links only TelemetryAbi.cpp from telemetry;
it does not compile either public JSON serializer. The independent qmake target
also enforces that separation:

```sh
mkdir -p build/no-json
cd build/no-json
qmake ../../tests/resources/no_json.pro CONFIG+=release QMAKE_CXXFLAGS+=-Werror
make -j2
./resource_no_json
```

The ordinary Qt playground keeps its public telemetry JSON UI. Build it from
telemetry.pro and use `-platform offscreen --smoke-test`. This migration changes
only resource payloads, not the library's field/command hot paths.

## Local measurements, 2026-09-22

The implementation was checked with Qt MinGW GCC 13.1, Clang 18 using ASan,
UBSan and float-cast-overflow instrumentation, and Cortex-M7 GCC 13.2.1 and
CubeIDE GCC 14.3.1. Both ARM optimizations compile and link. Qt 6.10.1 builds
and the offscreen smoke test passes, as does qmake with telemetry_no_json.

The following is the complete linked demo fixture, not the incremental cost
of this library. Published `f6d253a9` is the compact decimal JSON baseline.
Compiler: CubeIDE GCC 14.3.1, Cortex-M7 hard-float, nano/nosys, section GC.

| Fixture | `.text` | `.rodata` | `.data` | `.bss` |
|---|---:|---:|---:|---:|
| JSON baseline, O2 | 38236 | 2880 | 116 | 404 |
| Binary v1, O2 | 33784 | 2272 | 116 | 432 |
| JSON baseline, Os | 28132 | 2720 | 116 | 404 |
| Binary v1, Os | 22304 | 2208 | 116 | 432 |

Text plus read-only data decreases by 5060 bytes at O2 and 6340 at Os. Cached
metadata counts/fingerprints cost 28 additional bytes of BSS in this fixture.
The much earlier to_chars implementation had 104568 bytes of floating lookup
tables alone; both those tables and the replacement decimal formatter are now
absent. These are build measurements, not board cycle measurements.

GCC 14.3.1 individual read frames are:

| Function | O2 | Os |
|---|---:|---:|
| SchemaFile::read | 160 | 160 |
| CommandsFile::read | 176 | 152 |
| ValuesFile::read | 120 | 144 |

The old decimal helper alone needed 464/456 bytes. The largest new resource
frame across GCC 13/14 is 184 bytes. [stack_check.py](stack_check.py) pins
schema/command/value read budgets at 168/184/152 bytes and other emitted frames
at 192 bytes, with positive and negative controls and required-function checks.
These are per-frame limits, **not cumulative call-chain, owner or IRQ budgets**.
Nested metadata visitors and telemetry conversion calls still consume stack.

The linked fixture rejects snprintf, float printf support, localeconv, strtod,
to_chars, Ryu/Schubfach, pow and JSON schemaCrc symbols. Heap references are
checked separately in adapter/protocol objects. GCC 13 newlib retains malloc
symbols in its system signal object linked via abort; this is not an allocation
performed by the resource adapter. Its object/disassembly logs identify the
origin. Consequently the GCC 13 image is not described as heap-symbol-free.
Core dispatch probes reject runtime path comparison/strlen and retained indirect
calls for a known provider. No byte-format benchmark or hardware cycle result
is asserted here; the format change removes formatting work by construction.

Wire contract and lifetime/cursor rules: [binary adapter](../../lib/resource/telemetry/README.md).
Other modules: [core](../../lib/resource/README.md), [protocol](../../lib/resource/protocol/README.md).
