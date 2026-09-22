# Flat resources (C++20)

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](LICENSE).

`resource` depends only on the C++20 standard library. It knows neither a
transport protocol nor telemetry. A path is a borrowed flat label; slashes do
not imply nodes, directories, resolution or filesystem access.

The library keeps its optional modules beside the core headers:

```text
resource/
  Types.hpp, File.hpp, FileSystem.hpp, ChunkWriter.hpp
  resource.pri
  protocol/    Protocol.hpp, Protocol.cpp
  telemetry/   SchemaFile, CommandsFile, ValuesFile
```

One `resource.pri` connects the library to qmake. By default it adds the core
headers and packet protocol without a telemetry dependency:

```qmake
include(lib/resource/resource.pri)
```

Enable the telemetry providers explicitly when the application needs them:

```qmake
include(lib/telemetry/telemetry.pri)
CONFIG += resource_telemetry
include(lib/resource/resource.pri)
```

Set the option before the resource include; repeated includes add no duplicate
sources. Binary telemetry providers also work with `CONFIG += telemetry_no_json`.

```cpp
#include <resource/FileSystem.hpp>

class SettingsFile {
public:
    resource::FileSize size() const noexcept;
    resource::ReadResult read(resource::Cursor, resource::Output) const noexcept;
    resource::WriteResult write(resource::Cursor, resource::Input, bool final) noexcept;
};

SettingsFile settings;
constinit const auto files = resource::filesystem(
    resource::file("/settings.bin", settings));

// Later, after application initialization:
// files.read(0, 0, output);
// files.write(0, cursor, input, final);
```

For another build system, add the parent `lib` directory to the include path
and compile the sources of the modules you use. The core has no compiled
source, Qt, allocation, RTTI or virtual base class. Concepts require an exact
`uint32_t size() const noexcept`, and
at least one correctly typed `read` or `write` operation. `FileOps` holds the
generated function pointers; unavailable operations are null. `stat()` derives
Readable/Writable from these operations and queries the current size.

`FileIndex` and `FileSize` are `uint32_t`; `Cursor` is `uint64_t`. Identity is
the position starting at zero. Reordering the declarations changes identity.
`path()` returns an empty view for an invalid index; `stat/read/write` report
`InvalidFile`. `FileStat` includes a status so an empty file is distinguishable
from an invalid index. A provider returns an exact size representable in u32.

The table, provider and path text have independent lifetimes. `file()` rejects
temporary providers and owning temporary path strings. An explicitly created
`string_view` remains the caller's lifetime responsibility. Paths must start
with `/`, have nonempty components, and contain no control bytes, backslashes,
`.` or `..` components, or trailing slash. Duplicate paths are rejected.
These checks run during constant evaluation for constexpr/constinit definitions;
invalid runtime definitions terminate with `abort()`. There is no protocol
length limit in this library. Consumers supply valid UTF-8 labels if their UI
expects UTF-8.

`FileSystem<N>` owns only the descriptor array. `view()` returns a borrowed,
non-template `FileSystemView` for compiled boundaries and rejects a temporary
table. Lookup is one bounds check and direct indexing. Descriptor copies use
ordinary C++ copy construction, never raw packet serialization. Providers are
not copied. Const providers retain their const qualification in the callbacks.

## Transfer contract

- Cursor zero starts a transfer. Other values are defined by each provider;
  the core never compares them with file size or increments them.
- `ReadResult` returns status, next cursor, bytes written, and EOF. `WriteResult`
  returns status, next cursor, bytes consumed, and completion.
- On `Ok`, the byte count must not exceed the supplied span. An unfinished
  operation must consume/produce bytes or advance its cursor. Otherwise return
  a status such as `BufferTooSmall`; clients must check status and progress.
- On error, return the input cursor, zero bytes and false EOF/completion.
  Bytes already placed in the caller's output are not committed and must be
  discarded. Write providers must define their own handling of side effects.
- `final=true` marks the end of the submitted input stream. If a provider only
  consumes a prefix, the caller resubmits the unconsumed suffix with the returned
  cursor and the same final indication. Empty final input can finalize a stream.
  `complete` is the provider's acknowledgement, not a synonym for `final`.
- No repeated-write suppression exists. Every request reaches the provider.
  Synchronization, coherent snapshots and any persistent storage belong there.
- Spans are valid only during the synchronous operation; do not retain them.
  Core and callbacks assume valid C++ spans and live objects.

`ChunkWriter` has `writeAtomic` (all bytes or none) and `writePartial` (what fits).
It owns no cursor and permits overlapping input/output spans.

See [resource tests](../../tests/resources/README.md), the
[packet protocol](protocol/README.md), and
[telemetry adapters](telemetry/README.md).

The compact result ABI uses 16-byte ReadResult/WriteResult and 8-byte FileStat
on ARM32. Status-first brace construction remains supported through constexpr
constructors. These types are no longer aggregates; designated initializers
must become ordinary constructor calls. Rebuild all resource consumers after
this ABI change. The packet representation is independent and unchanged.
