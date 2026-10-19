# Code generator qualification, 2026-10-18

Baseline: `2b58484b597226397b1cef28de885ba004ee4afd` on `main`.
Generator version: `0.1.0`. No files under `lib/`, frozen goldens, historical
hardware receipts, Native/Encoded APIs or wire contracts were modified.
The existing Python Descriptor parser was promoted with a compatibility facade.

## Executed gates

| Gate | Result |
| --- | --- |
| Full GCC generated SDK suite, including ARM inspections and browser | 10,624 checked conditions |
| Clang 18 ASan/UBSan, recovery disabled | 10,453 checked conditions |
| Native C++ generated DTO through direct Encoded and RpcClient | 414 conditions in each compiler run |
| Standalone C++ / optional native Codec / allocation trap enabled | 83 / 98 / 83 conditions |
| JS and actual TS-compiled JS versus existing independent codec, five schemas | 9,434 conditions |
| Generated JS/TS through actual native Encoded/Resource RPC plus compiled client | 155 conditions per runtime |
| Actual Chromium ESM, JS and TS-compiled JS | 84 conditions per runtime |
| Generated codec on x86 little endian and s390x big endian through QEMU | 83 conditions each |
| Existing endian suite | 12 executions; five byte-identical wire files |
| Existing regression runners | 23 primary suites, six additional suites and shared ARM checks passed |
| Existing independent Windows qmake selections | All eight configurations passed |
| New CMake / Windows qmake examples | Three programs / standalone program passed |

The aggregate includes executed Python checks and offline ARM artifact
inspections. **No ARM instructions were executed on a physical STM32.**
Shared ARM qualification separately ran 336 compiler/inspection commands,
including 222 intended compile rejections and zero runtime MCU checks.

The primary old suites cover shared core, documentation examples, reflection,
facade, types, Codec, registry, Service, Model, mixed endpoints, Descriptor,
Resource, traversal, borrowed access, exchange, client, RPC, RpcClient,
RpcSessionPool, qualification, MCU host probes and freeze. Additional gates
cover ergonomic/flat APIs, storage policies, scalability runtime, COBS and ARM
freeze. Old parser mutation coverage retains 1,729 conditions.

The producer is a real [native Model](../tests/codegen/Device.hpp), exported by
[Producer.cpp](../tests/codegen/Producer.cpp). Its descriptor SHA-256 is
`9e784b3d634b4b82e646b5f3096b6b8d0f76dd05d5524e9403a8fa04a5596c19`.
Config, Service request/response, all scalar types, nested arrays and an empty
aggregate have independent native canonical payload fixtures. Generated output
is deterministic and `--check` verifies it without rewriting files.

## Negative controls and review fixes

The runner compiles a deliberately corrupted active little-endian C++ writer;
the runtime canonical-byte assertions must fail with exit 1. A JS endian writer
mutation must independently reach a runtime assertion and exit 1. An explicit
C++ allocation must reach the heap trap marker and exit 86. Mutation hashes,
commands and observed exits remain in each external `summary.json`.

Independent review found and fixed member names shadowing canonical TypeId
aliases, exported names shadowing JS globals, repeat getter reads truncating an
unchecked later value, and Encoded wrappers overwriting an active Workspace lease
before native refusal. Regression cases now preserve the lease and callback
counts on overlap. Sparse/empty and extreme 64-bit enum dictionaries have explicit
optional native reflection specializations. Accepted is checked with its operation
family; failed empty responses never reach a payload decoder.

## Cost measurements

The same generated 47-byte Scalars DTO is compiled through the generated codec
and existing native Codec. [Probe.cpp](../tests/codegen/Probe.cpp) uses noinline
encode/decode roots and a checked checksum over 200,000 pairs, repeated three
times. Host binary size is 16,176 bytes for both release programs in the retained
GCC 13.3 run. Host timings and compile seconds vary with scheduling and are kept
as raw samples in the runner receipt; this small benchmark supports no general
speedup claim or MCU timing claim.

Offline ARM GCC 13.2, Cortex-M7 hard-float, section GC, same two roots:

| Optimization | Generated `.text` | Native `.text` | Generated max individual frame | Native max individual frame |
| --- | ---: | ---: | ---: | ---: |
| O2 | 356 bytes | 348 bytes | 16 bytes | 16 bytes |
| Os | 494 bytes | 374 bytes | 32 bytes | 40 bytes |
| Og | 1,034 bytes | 3,944 bytes | 80 bytes | 96 bytes |

These minimal linked roots have no mutable static payload storage or allocation
symbols; caller-owned DTOs and buffers are additional application RAM. `.text`
excludes debug/comment sections and is not the size of an entire firmware.
Individual compiler frames do not establish cumulative call-chain stack usage.

For these linked GCC 13.2.1 Os roots, disassembly and static `.su` frames give a
closed, direct, acyclic call graph. Cumulative SP reservation from root entry
changes from 48 to 44 bytes for Scalars encode and 88 to 72 bytes for decode.
The large DTO remains at 24 bytes for encode and 32 bytes for decode. These bounds
exclude the root caller and interrupt/exception frames, and describe reserved
stack rather than observed stack writes.

A separate 10,001-byte DTO probe has maximum individual frames **16 / 24 / 80
bytes** for O2/Os/Og, no allocation symbols and a 256-byte enforced frame ceiling.
Arrays remain loops; decode validates before committing without a second DTO.

The final scalar fast path uses portable fixed-size `memcpy` only on little
endian, matching the existing native technique. Aggregate memory/padding is never
copied. Big endian retains explicit byte assembly and was executed through QEMU.
An earlier byte-shift implementation needed 800 bytes at ARM O2; the measured
leaf optimization reduced that to 356. A member-subspan candidate regressed both
size and host timing and was discarded.

An identical ARM GCC 13.2.1 probe reproduced the earlier generated O2/Os/Og
sizes of 356/590/972 bytes. Under Os, outlined aggregate helpers repeatedly
reloaded and stored the shared writer cursor across byte stores. Keeping that
cursor in a local Writer until the aggregate completes, and validating a bool
byte directly, reduced this probe by 96 bytes and its largest individual frame
from 48 to 32 bytes. O2 output stayed unchanged. Og grew by 62 bytes; the separate
10,001-byte DTO grew by 18 bytes at Og, with its O2/Os sizes and all three maximum
individual frames unchanged. Copying Reader state too was rejected: despite a
smaller Scalars probe, it increased the large DTO's Os maximum frame from 24 to
32 bytes. These are observations for the recorded probes, not guarantees for
every generated type or compiler.

## Reproduce and limits

See [runner commands](../tests/codegen/README.md), [CMake/qmake examples](../examples/codegen_cpp/README.md)
and [CLI](CodeGenerator.md). Logs, generated artifacts, input/tool hashes, stack
reports and measurements belong in the supplied external build directory.
The final GCC receipt compares source/tool byte snapshots before and after all
gates. Selected tool hashes are provenance, not a hermetic toolchain closure.

Generation supports the frozen Descriptor v3 type vocabulary and production
resource ceilings. It does not infer physical ranges/units/defaults, authenticate
a peer or add network delivery guarantees. JS 64-bit integers require bigint;
NaN payload bits are subject to Number semantics. JS `encodeInto` may leave an
earlier prefix written on a later validation error. Admission and status gates
are described in [schema compatibility](GeneratedSchemaCompatibility.md).
