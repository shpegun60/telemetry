# Browser Descriptor and payload helpers

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

For an owned host connection, start with [Connection Session](../doc/ConnectionSession.md):
`connectStatic({sdk, transport})` or `connectDynamic({transport})`, using the named
`demoWebSocket(origin)` reference-server profile or a custom `ConnectionProfile`.
It coordinates the existing FIFO, bootstrap, cancellation and local close. Advanced
client, codec and borrowed-carrier entry points remain available.

[telemetry.js](telemetry.js) validates Descriptor v3, provides runtime type and
endpoint lookup, and encodes/decodes values without a Generated SDK. It is used by
the [structured browser example](https://github.com/shpegun60/telemetry-ux/blob/a336b8c2984d5adf235b0791b545a37d3da3cc8b/examples/structured_client/README.md).
U64/S64 use JavaScript BigInt. This helper does not own an asynchronous request
queue, transport or connection lifecycle.

[DynamicClient.js](DynamicClient.js) adds an optional headless runtime session
over that parser and the existing asynchronous client FIFO. Bind the validated
model and exact peer header, cache endpoint handles, and use ordinary runtime
values for all supported operations. See the [checked usage and lifecycle
contract](../doc/DynamicJsClient.md) and [native parity tests](../tests/client_dynamic_web/README.md).
It introduces no DOM/Qt dependency or second execution engine.

For editing a previously read DTO, `encodeValuePreserving(model, typeId, edited,
originalPayload)` snapshots the original bytes and uses the same checked writer.
When an original floating leaf and its edited value are both NaN, its exact
original wire bits are retained. A finite edit uses ordinary encoding. Invalid
old Bool representations, lengths or new values refuse the whole operation;
neither caller buffer is modified. Plain `encodeValue` keeps its existing behavior.
`DynamicClient.writeFieldPreserving(endpoint, edited, originalPayload, timeoutMs)`
uses this codec through the same admission, FIFO and handoff path. The caller
must retain the corresponding FieldRead payload for the intended DTO/session.

For a known schema, `--wire-editing` adds the equivalent `encodePreserving` method
to generated JS/TS codecs and writable bindings. The UX web starter enables this
option automatically. Default generated output is unchanged.

For asynchronous Field/Command/Service requests, use the shared
[Client runtime](../lib/client/web/README.md) under `lib/client/web`. For known
schemas, the [generator](../doc/CodeGenerator.md) can instead emit standalone
TypeScript/JavaScript types and codecs. These are distinct usage paths, not
separate implementations of the client FIFO.

The local `package.json` declares ES modules and is private; this directory is
not a published package. The [example index](../examples/README.md) identifies
the maintained browser and generated-SDK entry points.
