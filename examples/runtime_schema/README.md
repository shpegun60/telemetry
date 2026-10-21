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
