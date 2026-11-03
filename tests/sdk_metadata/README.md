# Optional structural metadata qualification

The runner uses three frozen independent Descriptor fixtures and an explicit
edge fixture with signed/unsigned 64-bit enum codes, a signed narrow enum,
Unicode and escaped names, `__proto__`, an empty struct, a 65,536-element
zero-wire array, empty catalogs and colliding category IDs above 16 bits.

Both emitted JS and compiled strict TS modules are compared to JSON and the
independent JavaScript Descriptor parser, including every member's local wire
offset and every endpoint's category, ID, type and permissions. Mutation of
every nested metadata record is rejected. A real C++ program traverses every
generated record and string and compares exact enum bits and UTF-8 bytes.
Standalone compilation includes only `Metadata.hpp`; a second consumer also
includes the ordinary generated endpoints to catch namespace conflicts.

Default output is compared with opt-in output after excluding added files and
the manifest's explicit opt-in. Owned `--check`, opt-out removal and preservation
of unowned files are exercised. All writes and binaries go to fresh external
directories. Receipts retain commands, source/log/binary hashes and failures.

```sh
python3 -B tests/sdk_metadata/run.py --build-dir /external/metadata-gcc \
  --cxx g++ --node /path/to/node --tsc /path/to/typescript/bin/tsc
python3 -B tests/sdk_metadata/run.py --build-dir /external/metadata-clang \
  --cxx clang++-18 --sanitize --node /path/to/node --tsc /path/to/typescript/bin/tsc
```

Optional `--arm-cxx`, `--arm-nm` and `--arm-objdump` compile and link selected
Cortex-M7 roots under `-Os` and `-O2`, preserving maps, disassembly and `.su`
files. The linked roots require no allocator, initialization guard or libc.
This measures selected roots; it does not establish whole-firmware Flash,
peak stack or physical board behavior.
