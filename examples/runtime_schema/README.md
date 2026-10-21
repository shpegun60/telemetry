# Borrowed runtime value example

This console example reads the native fixture produced by the maintained suite.
It needs only `lib` in the include path and no compiled library source:

```sh
python3 -B tests/runtime_schema/run.py --cxx g++ --build-dir /external/runtime-schema
g++ -std=c++20 -O2 -Ilib examples/runtime_schema/main.cpp -o /external/runtime-reader
/external/runtime-reader /external/runtime-schema/native/descriptor.bin /external/runtime-schema/native/payload.bin
```

The descriptor, payload, metadata arrays and workspace remain alive and unchanged
while schema/handle/view objects are used. Resetting or reparsing the workspace
invalidates earlier schemas, views and prepared handles. This does not extend
the lifetime of freed storage or synchronize readers with a writer. The example
performs checked reads; it sends no requests and performs no writes.

`Writer.cpp` edits the same fixture in caller-owned staging storage, checks the
committed value through `DynamicValueView`, and saves the complete encoded value:

```sh
g++ -std=c++20 -O2 -Ilib examples/runtime_schema/Writer.cpp -o /external/runtime-writer
/external/runtime-writer /external/runtime-schema/native/descriptor.bin /external/runtime-schema/native/payload.bin /external/runtime-schema/edited.bin
```

The program changes `rows[1].bins[2]` to `7` and an open enum's underlying code
to `17`. It checks the field's Writable capability, but the runtime writer itself
is a wire-value editor: endpoint permission and transport admission remain the
application's responsibility. The output file is an offline artifact; no device
write, comparison with a current device value, or physical action is implied.

See [the runtime guide](../../doc/RuntimeSchema.md) for staging lifetime, sticky
failures and Read → Edit → Write → Refresh integration.
