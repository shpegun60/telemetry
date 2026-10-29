# Browser Descriptor and payload helpers

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[telemetry.js](telemetry.js) validates Descriptor v3, provides runtime type and
endpoint lookup, and encodes/decodes values without a Generated SDK. It is used by
the [structured browser example](../examples/structured_client/README.md).
U64/S64 use JavaScript BigInt. This helper does not own an asynchronous request
queue, transport or connection lifecycle.

For asynchronous Field/Command/Service requests, use the shared
[Client runtime](../lib/client/web/README.md) under `lib/client/web`. For known
schemas, the [generator](../doc/CodeGenerator.md) can instead emit standalone
TypeScript/JavaScript types and codecs. These are distinct usage paths, not
separate implementations of the client FIFO.

The local `package.json` declares ES modules and is private; this directory is
not a published package. The [example index](../examples/README.md) identifies
the maintained browser and generated-SDK entry points.
