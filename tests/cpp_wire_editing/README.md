# Optional standalone C++ wire editing

Generate `cpp` with `--wire-editing` to add:

```cpp
template<class T>
[[nodiscard]] codec::Status codec::encodePreserving(
    const T& value, std::span<const std::byte> original,
    std::span<std::byte> output) noexcept;
```

`original` is the stable canonical payload of the same generated type. Both
spans must have exactly `codec::wireSize<T>` bytes. The original bytes and native
value must be disjoint from output. Length, overlap and complete canonical
validation precede all writes; a refused call leaves output unchanged. Empty
structures and zero-length arrays are valid zero-byte values.

When the old and new float values are both NaN, the writer copies the original
IEEE representation, including sign, payload and signaling bit. A finite new
value, infinity or signed zero uses its new representation. Integers, booleans,
enums, structures and arrays follow the existing generated codec. There is no
allocation, scratch DTO, runtime Descriptor parser, Qt or transport dependency.
The caller keeps input stable for the synchronous call; this function does not
snapshot an aliased or concurrently changing seed.

`--ux-starter qml` requires `--targets qml --structural-metadata` and enables this
writer automatically. It requires the matching installed UX extension to expose
`generate_qml_configurator(schema)`. The ordinary QML generator and default C++
output remain unchanged. An existing web starter by itself still changes only
its JS/TS codecs; add explicit `--wire-editing` to enable the C++ helper too.

Run with a fresh external directory:

```sh
python3 -B tests/cpp_wire_editing/run.py --cxx g++ \
  --arm-cxx arm-none-eabi-g++ --build-dir /tmp/telemetry-cpp-writer-gcc
python3 -B tests/cpp_wire_editing/run.py --cxx clang++ --sanitize \
  --build-dir /tmp/telemetry-cpp-writer-clang
```

The runner exports an actual native Descriptor, generates standalone C++ and
executes its public API. Exact-byte oracles cover positive/negative quiet and
signaling NaNs in both widths, both zeros, infinities, changed finite values,
nested arrays, unknown enum codes and unaligned spans. Invalid final booleans,
wrong lengths and overlapping buffers must fail without changing output.
Allocation replacement is exercised positively and with six intentional exit-86
controls. Two generated-header mutations must fail the byte/canonical oracles.
Default output must fail to compile a real `encodePreserving` call. Optional ARM
roots compile at `-Os` and `-O2` and reject allocation/static-lifetime symbols.

The CLI controls test the optional extension routing and guarded refusal before
publication, using an explicit fake extension. They do not qualify a Qt provider.
The installed UX extension suite separately retains the 282 default-output
golden hashes. Source hashes bracket execution; the receipt is an offline host
qualification, not a hardware or remote-CI claim.
