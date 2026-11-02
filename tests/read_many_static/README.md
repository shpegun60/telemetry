# Static typed ReadMany qualification

`Example.cpp` is the short successful API example. It selects generated fields,
binds the peer capability once and reads checked native values. Only its test
connection fixture owns bytes and dispatches the request; the user API does not
build request or reply payloads. `Standalone.cpp` separately compiles against
the minimal installed client package without Native Model, RuntimeSchema,
DynamicClient, Inspector or Qt. It uses one field and the unchanged default
64-byte callback capacity, then closes before activation to check cancellation.
It does not claim to have received a successful value.

Run from the repository root with a fresh external directory:

```sh
python3 -B tests/read_many_static/run.py \
  --cxx g++ --build-dir /absolute/external/static-many-gcc
python3 -B tests/read_many_static/run.py \
  --cxx clang++-18 --sanitize --build-dir /absolute/external/static-many-asan
```

The runner exports an actual native Descriptor, runs the existing C++ generator,
installs selected SDK components, moves each install prefix, and builds consumers
against those relocated packages. It records source/generated/tool hashes,
commands, logs, executable results and compiler stack records. Nothing is deleted.

`Host.cpp` exercises generated and independent handwritten scalar codecs and
handwritten endpoints; 1/8/12/16/32 fields across nonzero catalog IDs; exact
ordering and per-item statuses; all supported scalar widths and open enum;
bit-preserved signed qNaN/sNaN payloads and zeros; callback-owned copies; shared
FIFO with ordinary typed reads; bounded refusal; stale bindings and newer
sessions; fake-clock queued/offered/prepared cancellation and timeout boundaries;
malformed replies including a valid earlier item followed by noncanonical Bool;
callable move/destructor reentry, null/free-function/move-only and false-bool
callables; decode-time invalidation by a handwritten codec. Operation results and
value availability are checked separately.

`NoHeap.cpp` repeats the host suite under global new/delete and linker allocation
traps. Six separate controls must exit 86 for new, aligned new, malloc, calloc,
realloc and free. `Negative.cpp` must fail compilation at public API boundaries
for unsupported fields/types, widened IDs, missing codec identity, wrong wire
width, invalid callable ownership/signature, insufficient callback storage and
attempts to forge private binding/value state.

An offline ARM profile accepts the intended compiler explicitly:

```sh
python3 -B tests/read_many_static/run.py --arm \
  --cxx /path/to/arm-none-eabi-g++ --producer-cxx g++ \
  --build-dir /absolute/external/static-many-arm
```

It links selected generated/handwritten operation roots at `-Os` and `-O2`, records
sections, symbols, disassembly and `.su` compiler frames, and rejects allocation
symbols. The entry point takes caller-owned admitted clients and concrete
backends; their creation, vtables and transport buffers are outside this selected
root. In particular, a virtual deleting backend destructor is not part of a
client-only allocation claim. Host tests and `Example.cpp` still construct and
exercise real Encoded backends. These are offline compiler observations, not full-firmware Flash deltas,
physical high-water stack measurements, MCU execution or UART/DMA qualification.
