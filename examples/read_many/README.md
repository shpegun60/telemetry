# ReadMany host example

Reads Voltage, exact U64 Energy and Enabled from two catalogs with the real
Descriptor, RuntimeSchema, DynamicClient FIFO and opt-in Encoded dispatcher.
The ordinary Resource BytesFile capability uses application-owned storage.
No board, Qt, generated SDK or external COBS checkout is needed.

```sh
cmake -S . -B /external/telemetry -G Ninja \
  '-DTELEMETRY_COMPONENTS=read_many_client;encoded_backend;encoded_server;core;resource' \
  -DCMAKE_INSTALL_PREFIX=/external/package
cmake --build /external/telemetry
cmake --install /external/telemetry
cmake -S examples/read_many -B /external/example -G Ninja \
  -DTelemetry_DIR=/external/package/lib/cmake/Telemetry
cmake --build /external/example
/external/example/read-many-host
```

Output includes `Energy=18446744073709551615` without a floating conversion.
Views are borrowed inside the completion. The synchronous port owns its TX copy;
an asynchronous/DMA port must retain that copy until physical TX release.
See [wire and lifecycle contract](../../doc/ReadManyEncoded.md).
