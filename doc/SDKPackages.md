# Installed SDK packages

The development SDK version is `0.1.0`. C++ modules, the private ESM package and
the offline Python generator can be installed independently. These are local
package workflows; no npm/PyPI release or repository rename is implied.

SDK `0.1.0`, structural metadata format `1`, Descriptor wire `3.0` and Encoded
wire `1` are separate version domains. Changing package metadata does not change
the wire protocol.

## C++ modules

Select the modules needed by the application and install into an external prefix:

```sh
cmake -S /path/to/telemetry -B /external/sdk-build -G Ninja \
  '-DTELEMETRY_COMPONENTS=read_many_static_client;batch_static_client;encoded_backend' \
  -DCMAKE_INSTALL_PREFIX=/external/sdk-prefix
cmake --build /external/sdk-build --target install
```

The consumer uses only the installed prefix:

```cmake
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS
    read_many_static_client batch_static_client encoded_backend)
target_link_libraries(my_app PRIVATE Telemetry::read_many_static_client
    Telemetry::batch_static_client Telemetry::encoded_backend)
```

Pass `-DCMAKE_PREFIX_PATH=/external/sdk-prefix` when configuring the consumer.
Include paths, `Telemetry::` targets and C++ namespaces retain their existing
names. The initial package policy accepts an exact version; it does not promise
compatibility with every other `0.x` package. The prefix can be relocated.
An installation containing only interface/header modules is architecture
independent. A prefix containing compiled libraries retains the architecture
check, so build it with the intended target toolchain.

`SDKMetadata.json` under the installed data directory records the package and
wire versions. A controlled packager may set `TELEMETRY_SOURCE_REVISION` to a
40-digit Git revision. The default is empty; that optional string does not by
itself certify a clean source tree or a successful qualification run.

## Headless JavaScript and TypeScript

Create a local tarball, then install it into the application:

```sh
npm pack /path/to/telemetry --pack-destination /external/packages
cd /path/to/my-app
npm install --offline --ignore-scripts --no-audit --no-fund \
  /external/packages/telemetry-sdk-0.1.0.tgz
```

The package is private and has no third-party runtime dependencies. Its public
entrypoints contain the canonical SDK implementation and matching declarations:

```ts
import {DeviceClient, EncodedBackend, Batch} from 'telemetry-sdk';
import {parseDescriptor} from 'telemetry-sdk/runtime';
import {DynamicClient} from 'telemetry-sdk/dynamic';
```

`telemetry-sdk/client` is an explicit alias for the default client entrypoint.
The runtime modules need no Qt or `document`/UI framework. They use standard
JavaScript APIs such as Promise, AbortController and BigInt. Existing source-tree
imports remain supported; new applications need not copy SDK files into their UI.

The [typed Batch](../tests/client_batch_web/README.md) and
[runtime client](../tests/client_dynamic_web/README.md) consumers demonstrate the
two client styles. Use a separate backend instance for each independently owned
client/session. One client owns its FIFO.

## Offline Python generator

Install a locally built wheel into an application environment:

```sh
python3 -m venv /external/codegen-env
/external/codegen-env/bin/python -m pip install --no-index \
  /external/packages/telemetry_codegen-0.1.0-py3-none-any.whl
/external/codegen-env/bin/telemetry-codegen generate descriptor.bin \
  --targets cpp,js,ts --client-runtime --structural-metadata \
  --output /external/generated-sdk
```

On Windows, use the environment's `Scripts/python.exe` and
`Scripts/telemetry-codegen.exe`. `python -m tools.telemetry_codegen` remains a
compatible module invocation, including from the installed environment.

The generator defaults to `cpp,ts,js`. `--client-runtime` includes the same
canonical client source/distribution assets packaged in the wheel; an SDK
checkout is not required at generation time. Missing installed assets produce
an error before output publication. `--structural-metadata` is opt-in and leaves
the normal generated files unchanged. `--check` verifies existing owned output
without rewriting it. See [code generation](CodeGenerator.md) for output ownership.

The optional `--targets qml` remains available and includes the C++ target
automatically. Emitting it needs no Qt Python runtime; compiling its generated
module still needs the documented Qt kit. UI code has not moved to another
repository as part of this packaging change.

Maintainers build wheels from an external prepared source copy so setuptools
build output does not enter the SDK checkout. The
[package qualification runner](../tests/sdk_packages/README.md) builds through
a source archive and then tests installed consumers with that source unavailable.

## Qualification boundary

The maintained package suite checks relocated CMake consumption and incompatible
version refusal, a packed/installed ESM consumer and strict TypeScript imports,
and a wheel-installed CLI with canonical runtime resources. It performs no
registry publication, hardware access or physical transport qualification.
