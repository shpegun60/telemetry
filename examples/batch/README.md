# Encoded Mixed Batch

Build the SDK with `batch_client;batch_server;encoded_backend`, install it, then:

```sh
cmake -S examples/batch -B /tmp/telemetry-batch-example \
  -DTelemetry_DIR=/path/to/install/lib/cmake/Telemetry
cmake --build /tmp/telemetry-batch-example
/tmp/telemetry-batch-example/batch-host
```

Expected output:

```text
Write Applied; Command Executed; Service=28; Read=100
```

The real Host Model executes Write, Command, Service, then Read of the same Field
through one DynamicClient FIFO and the complete-frame Encoded dispatcher.
The client discovers endpoint IDs/types from Descriptor v3. The input scalars
use the existing native Codec; arbitrary runtime DTOs can use DynamicValueWriter.
No new serialization implementation is required.

TBM1 and local backend support are explicitly enabled. Default EncodedBackend
does not retry. Choosing RetryingEncodedBackend permits repeated execution and
requires the application's agreement; this is not an exactly-once transaction.
There is no socket, physical board, second FIFO or hidden transport here.
See the [full contract](../../doc/MixedBatchEncoded.md).
