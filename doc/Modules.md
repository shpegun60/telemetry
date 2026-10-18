# Independent modules

Telemetry has five functional modules. They remain in one repository and can
be selected, built and installed independently.

| Module | Sources | Responsibility |
| --- | --- | --- |
| Telemetry | `lib/telemetry` | Definitions, iteration, Native, Encoded, Codec and descriptor construction |
| Resource | `lib/resource` | Files, providers and the generic file protocol |
| Client | `lib/client` | Async requests, queue, callbacks, deadlines, cancellation, shutdown and native RPC packet engine |
| Integration | `lib/integration` | Optional Resource providers, server dispatchers and client backends |
| Generator | `tools/telemetry_codegen` | Host tool converting a Descriptor into an SDK |

`lib/common` supplies the existing fixed-storage callable utilities through
`tiny_delegate`. `lib/wire` supplies the shared TEQ1 and RPC byte contracts.
Neither utility requires a Model, a filesystem, a client or an operating system.
Vendored dependencies keep their existing pinned contents and licenses.

Telemetry and Resource do not include or link to each other or to Client.
Client core uses the standard library and common callable utilities. Generated
ClientBindings supplies standalone DTO codecs; a local Model is unnecessary.
Native RPC is an optional Client component with native Codec and Resource
packet dependencies. The native C++ backends belong to Integration.

The portable JS/TS runtime in `lib/client/web` remains a single distribution
containing DeviceClient and its Encoded/RPC adapters. The generator copies it
only when `--client-runtime` is selected. The C++ source and target boundaries
described here do not imply separate JS packages; generated standalone web
codecs do not require that runtime.

| API | Canonical include | Namespace |
| --- | --- | --- |
| Local Model | `telemetry/Telemetry.hpp` | `telemetry` |
| Protocol-free dispatcher | `telemetry/encoded/Dispatcher.hpp` | `telemetry` |
| Descriptor builder/reader | `telemetry/descriptor/Descriptor.hpp` | `telemetry::descriptor` |
| Generic files | `resource/Resource.hpp` | `resource` |
| Portable client | `client/DeviceClient.hpp` | `telemetry::client` |
| Native RPC engine | `client/rpc/RpcClient.hpp` | `telemetry::client::rpc` |
| Shared TEQ1/RPC contracts | `wire/EncodedWire.hpp`, `wire/RpcWire.hpp` | `telemetry::protocol::encoded`, `telemetry::protocol::rpc` |
| Resource providers | `integration/telemetry/v3/DescriptorFile.hpp`, `ValuesFile.hpp`, `RpcMailbox.hpp`, `RpcSessionPool.hpp` | `resource::telemetry::v3` |
| TEQ1 server | `integration/encoded/Dispatcher.hpp` | `telemetry::integration::encoded` |
| Encoded backend | `integration/encoded/Backend.hpp` | `telemetry::integration::encoded` |
| RPC backend | `integration/rpc/Backend.hpp` | `telemetry::integration::rpc` |

The Resource provider namespace denotes an extension implemented by Integration;
generic Resource contains no Telemetry headers or providers. The descriptor
builder is independent of Resource read results. DescriptorFile performs the
actual result mapping at the file boundary. Values retains its existing wire
format and sampling behavior.

Old Client and Resource/Telemetry include trees and compatibility facades are
removed. Applications must update their includes and client/transport namespaces
and rebuild their C++ modules. Descriptor v3.0, RPC v1.0, TEQ1 and Resource bytes
remain unchanged. Native endpoint definitions and DeviceClient behavior remain
unchanged.

## CMake

```sh
cmake -S . -B /tmp/telemetry-core -DTELEMETRY_COMPONENTS=core \
  -DCMAKE_INSTALL_PREFIX=/tmp/telemetry-install
cmake --build /tmp/telemetry-core
cmake --install /tmp/telemetry-core
```

Downstream projects use the installed package:

```cmake
find_package(Telemetry REQUIRED COMPONENTS core)
target_link_libraries(application PRIVATE Telemetry::core)
```

Available components are `core`, `resource`, `resource_protocol`, `client`,
`codec`, `resource_adapter`, `encoded_server`, `rpc_server`, `rpc_sessions`,
`encoded_backend`, `rpc_client` and `rpc_backend`. Shared header components
`common`, `delegate`, `reflection`, `resource_wire`, `encoded_wire` and `rpc_wire`
are selected automatically as needed. Multiple components use a semicolon list.
The native RPC engine is `rpc_client`; its DeviceClient adapter is `rpc_backend`.

Each target exports only its own headers and declared dependencies. Neither
in-tree targets nor installed exports grant access to the entire `lib` tree.
Each installed header directory is owned by its component; upgrading removes
obsolete headers from that directory. Installation prefixes are relocatable.

## qmake

Each module owns one manifest beside its sources: `telemetry.pri`,
`resource.pri`, `client.pri`, `integration.pri`, `common.pri` and `wire.pri`.
The header-only native Codec submodule owns `telemetry/codec/codec.pri`.

Select Integration explicitly. Existing selection names `resource_telemetry`,
`resource_telemetry_rpc`, `resource_telemetry_rpc_sessions` and
`resource_telemetry_encoded` select the corresponding providers/server. Use
`telemetry_encoded_backend` or `telemetry_rpc_backend` for client adapters.
`telemetry_client_rpc` selects only the native RPC packet engine.

```qmake
CONFIG += telemetry_encoded_backend
include(path/to/telemetry/lib/integration/integration.pri)
```

Repeated inclusion does not duplicate compiled sources. The generic Resource
manifest has no Telemetry selections. The Generator remains a Python host tool;
standalone generated codecs have no Client, Resource, Qt or Model dependency.

## Verification

`tests/modules/run.py` builds isolated components, installs and relocates them,
then builds independent downstream projects. Positive controls accompany
expected include and link rejections. The suite checks removed headers on
reconfigure/upgrade and refuses source changes during qualification. `--arm`
compiles and links Cortex-M7 images without executing them.

Focused dispatcher, descriptor, client, SDK and wire regressions cover preflight,
overlap, error precedence, family statuses and exact bytes. Runtime sanitizers
and the existing CI matrix remain required. All build outputs and receipts
belong outside the source repository. Historical hardware evidence retains its
original source identity; this refactoring does not establish new board results.
