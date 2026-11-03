# Dynamic JS/TS client

The runtime API uses `parseDescriptor()` and one opaque schema/session binding.
It owns one existing DeviceClient FIFO. Endpoint handles cannot bypass the bound
Descriptor or select the static generated API. Runtime DTOs remain `unknown` in
TypeScript until the application checks their structure.

The native Model is shared with `../batch_static`, with separately admitted
TRM1 and TBM1 capabilities. `Integration.mjs` compares exact request/reply bytes,
endpoint outcomes and independently recorded native execution traces between
the generated static API and runtime API for all six operations. It also queues
all six runtime operations through the same FIFO.

Once the application has obtained a Descriptor and opened a physical session:

```js
const schema = parseDescriptor(descriptorBytes);
if (client.admit(schema, observedHeader, session) !== Status.Ok)
    return;
const selected = client.bind('FieldRead', selectedField.id);
if (selected.status !== Status.Ok)
    return;
const result = await client.readField(selected.endpoint);
if (result.operation.succeeded && result.valueStatus === ValueStatus.Ok)
    console.log(result.value);
```

`Consumer.ts` compiles this style without generated DTOs or type assertions;
runtime values remain `unknown` until the application narrows them.
`Check.mjs` covers parser/header identity, stale handles, the producer lifetime,
queued/offered/prepared invalidation, canonical failure, checked-result
projection, physical TX release, retry suppression, explicit quiescence and
the RPC partial/final WRITE boundary. A denied post-final Resource READ has no
new ACK timer or physical exchange and waits for the original overall deadline.

Descriptor member names remain their original strings. Both runtime and JS/TS
generated DTOs use the original member `number`; C++ generated DTOs normalize
that reserved type name to `number_value`. Wire parity also compares payload
bytes and endpoint outcomes independently of those language naming rules.

All generated SDKs, binaries and logs belong in external build directories.
The runner requires fresh outputs, checks published JS against its strict TS
source, executes both runtime/generated binding pairs, and records native
executable hashes and unchanged source inputs. GCC and Clang ASan/UBSan profiles
cover the native peer; JavaScript checks execute in Node. No physical board or
browser result is implied.

`Preserving.mjs` exercises typed FieldWrite with a retained wire seed. It checks
both signs of quiet/signaling F32/F64 NaNs and signed zero, finite edits, offset
views, malformed seed length/Bool, failure without mutation, getter reentry and
seed ownership. Closed, revoked and QueueFull paths do not touch seed/value
getters. The same 97 controls run with published JS and the compiled TS client.

```sh
python3 -B tests/client_dynamic_web/run.py --build-dir /external/dynamic-gcc \
  --cxx g++ --node /path/to/node --tsc /path/to/typescript/lib/tsc.js
python3 -B tests/client_dynamic_web/run.py --build-dir /external/dynamic-clang \
  --cxx clang++-18 --sanitize --node /path/to/node --tsc /path/to/typescript/lib/tsc.js
```
