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
- BinaryCheck: 797621 checks of exact scalar patterns, signed minima, U64 max,
  bool/Null, negative zero, subnormals, NaN payloads and infinities. Partial
  writes at every scalar offset, raw strings including NUL/UTF-8, checked u32
  length overflow and 80000 random typed payloads are covered. FNV-1a 64 checks
  use RFC 9923 vectors, independent two-word arithmetic, all 4096 random byte
  prefixes and chunked/empty updates.
- StreamCheck: hierarchical block offsets, empty/tiny buffers, atomic
  preflight, output guards, EOF and invalid cursors.
- TelemetryFilesCheck: 24253 checks with an independent binary reader against
  the real metadata. Full-file goldens pin schema, commands and values bytes,
  including fingerprints. Chunk capacities 1/2/3/7/31/63/127/220/256/1024 and
  every block byte offset reconstruct identical metadata. Checks cover
  min/max/default, enum dictionaries, maximum packed ID, empty catalogs,
  reserved fields, labels with quotes/newlines/UTF-8 and enum names with NUL,
  null versus empty parameter labels, flags/fingerprint changes, atomic getter
  counts, changing readings, unavailable zero payloads and protocol READ paging.
  Every values-header byte offset is checked, including both fingerprint words.
- MetadataSizeCheck: 51440 checks compare checked arithmetic sizes with actual
  encodings over all native Scalar types and 128 label lengths. They cover
  nullable labels, embedded NUL, enum records, u32 overflow, and rejection of
  all invalid internal ScalarType tags in field and parameter descriptors.
- LocalityCheck: 65536 catalogs with instrumented metadata callbacks prove that
  resuming the final field/command visits no earlier descriptor or getter.
- DecoderCheck: Node runs the actual browser ES module against the C++ fixtures.
  Exact BigInt/signed/float values, unknown type-99 records with 17-byte payloads,
  header extensions, unsupported major/minor versions, 1667 truncation cases, invalid lengths/counts/status,
  nonzero byteOffset views and 6000 deterministic packet mutations are covered.
  All 64 single-bit fingerprint mismatches are rejected. Published v1 goldens
  are rejected before their old headers can be misread. Independent BigInt
  hashing reconstructs semantic postorder from the complete wire fixtures.
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
  SchemaFile/CommandsFile/ValuesFile are 36/36/24 bytes: each cached fingerprint
  costs exactly four more bytes than v1. The FNV kernel must contain hardware
  UMULL and no helper calls; resource objects reject software 64-bit multiply.

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
| Binary v1 (`5885caa`), O2 | 33784 | 2272 | 116 | 432 |
| Binary v2 (FNV-1a 64), O2 | 33504 | 2272 | 116 | 444 |
| JSON baseline, Os | 28132 | 2720 | 116 | 404 |
| Binary v1 (`5885caa`), Os | 22304 | 2208 | 116 | 432 |
| Binary v2 (FNV-1a 64), Os | 22440 | 2208 | 116 | 444 |

V2 reduces text plus read-only data by 5340 bytes at O2 and 6204 at Os against
the JSON baseline. Against v1 it saves 280 bytes at O2 and adds 136 at Os.
The shared construction-time hash loop avoids repeated inlining into writer
primitives. The three cached fingerprints add 12 bytes of BSS over v1 (40
over the JSON baseline). They are never recomputed during values reads.
The much earlier to_chars implementation had 104568 bytes of floating lookup
tables alone; both those tables and the replacement decimal formatter are now
absent. These are build measurements, not board cycle measurements.

V2 individual read frames in bytes are:

| Function | GCC 13 O2 | GCC 13 Os | GCC 14 O2 | GCC 14 Os |
|---|---:|---:|---:|---:|
| SchemaFile::read | 168 | 160 | 168 | 160 |
| CommandsFile::read | 192 | 160 | 176 | 152 |
| ValuesFile::read | 120 | 152 | 128 | 152 |

The old decimal helper alone needed 464/456 bytes. The largest v2 resource
frame across GCC 13/14 is 192 bytes. GCC 13's command read at O2 grows by eight
bytes over v1; this is an explicit cost of the change. Values reads grow by
eight bytes on both compilers. [stack_check.py](stack_check.py) pins
schema/command/value read budgets at 168/192/152 bytes and other emitted frames
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
calls for a known provider. Compared with the rebuilt v1 baseline, their ARM
instructions/relocations are unchanged at O2/Os, as are the complete DemoCatalog
and TelemetryAbi objects. Replacing literal state bytes with wire enum constants
was checked separately: all three adapter instruction streams were unchanged.
No hardware cycle result is asserted here.

Wire contract and lifetime/cursor rules: [binary adapter](../../lib/resource/telemetry/README.md).
Other modules: [core](../../lib/resource/README.md), [protocol](../../lib/resource/protocol/README.md).

## Direct cursor stage

Baseline v2 bytes and fingerprints stay fixed. `LocalityCheck.cpp` uses test-only
metadata counters and 65536 catalogs: resuming the last field/command calls no
earlier metadata. Values sample only the selected field after complete preflight.
The max-ID fixture reaches `0xffffffff` and canonical EOF. BlockStream checks
30-bit size limits without allocating giant buffers. Malformed prefix/catalog/EOF
keys, invalid offsets and repeated EOF are explicit controls.

The intermediate direct-cursor implementation still uses a counting writer in
READ. GCC 14 O2 reports Schema read 224 B, Command read 184 B, Values read 120 B.
The schema exceeds the retained 168 B guard; the guard is deliberately unchanged.
The following single-encode stage must resolve this before final publication.
This checkpoint is a functional comparison, not the final performance result.

## Single-encode stage

READ now prepares string references once per processed record, calculates its
length with checked arithmetic, then encodes it at most once. Skipped records
run no encoder. The shared wire primitives are specialized for construction-time
count/hash or bounded output; READ stores no hash pointer or measuring flag.
Each min/max/default temporary ends before the next is created. Values use a
dedicated fixed-token loop, with one complete-token reservation before its getter.

The v2 Golden.hpp payloads, 64-bit semantic fingerprints and browser decoder
are unchanged. All earlier cursor boundary and locality controls still pass.
The intermediate stage's stack regression is resolved without raising any limit:

| Individual READ frame | GCC 13 O2 | GCC 13 Os | GCC 14 O2 | GCC 14 Os |
|---|---:|---:|---:|---:|
| SchemaFile | 168 | 160 | 168 | 160 |
| CommandsFile | 112 | 96 | 112 | 96 |
| ValuesFile | 112 | 128 | 120 | 120 |

These numbers still exclude nested calls, owner callbacks and IRQs. The complete
linked demo fixture with CubeIDE GCC 14.3.1 has:

| Optimization | `.text` | `.rodata` | `.data` | `.bss` | text + rodata vs f1cfbd8 |
|---|---:|---:|---:|---:|---:|
| O2 | 32880 | 2336 | 116 | 444 | -560 B |
| Os | 24760 | 2272 | 116 | 444 | +2384 B |

The Os increase is an explicit code-size cost of direct block dispatch and
specialized bounded encoding. These are build measurements, not cycle claims.
The fixture links no decimal formatter or JSON serializer. Resource objects
contain no allocation references or software 64-bit multiply. The selected
values lookup has no call or loop; a backwards jump to a shared return is allowed
as normal compiler tail merging and is not mistaken for a traversal loop.

[H7S harness](h7s/README.md) compares the exact f1cfbd8 baseline with current source,
including first/last entry READ, sequential enum/parameter visitors and complete
nested stack watermarks. It retains input/image hashes and restores the saved
64 KiB image before reporting success.

The [completed H7S measurements](h7s/README.md#measured-completion-2026-09-22)
now include both optimizations, complete file transfers, error preflight and
nested PSP watermarks. Last-group reads improve substantially; the report also
records the 5.9% Os sequential-parameter regression and the H7RS MPU startup fix.
Both Flash restoration and resumed original-firmware execution were verified.

## Binary 2.1 protocol checks

The protocol checks LIST path lengths 65533, 65534, 65535 and 65536 with full and
undersized reply buffers. Only 65533 fits a u16 payload plus its two-byte length;
larger paths report InvalidData instead of requesting an impossible larger packet.
The generic file descriptor's path contract remains protocol-independent.

Commands now preserve reserved positions via `commandFlags & 1`. Tests use equal
names, positions and zero arity to distinguish a reserved descriptor from a real
command, compare fingerprints, and verify that an empty function slot is still a
real command. Each fixture is transferred at every record boundary and decoded
independently by JS. All three files emit 2.1. The decoder accepts exactly that
version; unsupported major and minor versions are rejected before interpretation.
The updated goldens keep record layouts and sizes unchanged while pinning the new
version bytes and fingerprints. Version bytes belong to the hash domain; cached
2.0 metadata must be discarded even for catalogs without reserved commands.

Decoder checks toggle every record-header flag bit on every known v1 record,
reject unsupported capability/parameter bits and contradictory reserved records,
and retain unknown policy bits. Unknown record types/versions remain opaque.
The host, sanitizer and Cortex-M7 checks cover these changes. Individual ARM
READ stack budgets and the selected-value direct-lookup guard remain unchanged.
The startup pattern, descriptor layout and core implementation are unchanged.
The LIST condition is checked by the protocol tests; that unused protocol entry
point is removed from the linked demo fixture by section GC.

GCC 14.3.1 reports `.text` 32936/24792 bytes at O2/Os: +56/+32 bytes versus
260e168/c5739b2. `.rodata` remains 2336/2272, `.data` 116 and `.bss` 444 bytes.
DeviceResources, DemoCatalog, TelemetryAbi and ArmProbe objects remain identical
after removing debug/comment sections. The file table still occupies 48 bytes
in `.rodata`. The version constants and reserved-command encoding change the
resource providers; no new MCU timing claim is inferred from this build.
