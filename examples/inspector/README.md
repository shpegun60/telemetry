# Read-only Inspector host example

This application uses the portable [Inspector Model](../../doc/InspectorModel.md)
without Qt or a generated client SDK. The default local peer supplies a real
Descriptor and handles real Encoded FieldRead requests through its dispatcher.
It lists all three endpoint categories, then reads a Voltage structure and a
U64 counter. Commands and Services are listed but never called.

All displayed measurements are demonstration values, not STM32 readings.

Build and install the required components, then build this standalone consumer:

```sh
cmake -S . -B /tmp/telemetry-inspector-lib \
  '-DTELEMETRY_COMPONENTS=inspector;encoded_backend;encoded_server;core' \
  -DCMAKE_INSTALL_PREFIX=/tmp/telemetry-inspector-install
cmake --build /tmp/telemetry-inspector-lib
cmake --install /tmp/telemetry-inspector-lib
cmake -S examples/inspector -B /tmp/telemetry-inspector-example \
  -DCMAKE_PREFIX_PATH=/tmp/telemetry-inspector-install
cmake --build /tmp/telemetry-inspector-example
ctest --test-dir /tmp/telemetry-inspector-example --output-on-failure
/tmp/telemetry-inspector-example/inspector-host
```

The library model allocates no heap memory. This host application's optional
file loader owns a vector of Descriptor bytes and fixed-capacity runtime index
arrays. Larger schemas require larger application index arrays; parser Capacity
is reported without replacing those limits.

To browse a Descriptor file without a connected transport:

```sh
/tmp/telemetry-inspector-example/inspector-host --descriptor device.bin
```

This mode lists only metadata. It does not send the file's endpoint IDs to the
demonstration peer or claim live values. `--write-demo-descriptor FILE` writes the
local peer's Descriptor for a reproducible file-input example. The three CTest
cases exercise the loopback read and the file export/import path.

The local synchronous port owns a TX copy until dispatch returns. An asynchronous
UART/TCP port must separately retain its TX storage until physical release and
follow the [DynamicClient recovery lifecycle](../../doc/DynamicClientUsage.md).
Descriptor transfer through Resource and physical STM32 qualification are not
implemented by this example.
