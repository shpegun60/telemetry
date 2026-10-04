# Add your own resource files

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

[Files.cpp](Files.cpp) is a complete C++20 program with three files:

| Index | Path | Provider | Access |
| --- | --- | --- | --- |
| 0 | `/device/version.bin` | `BytesFile`, immutable array | Read |
| 1 | `/user/settings.bin` | `SettingsFile`, 16-byte RAM storage | Read/write |
| 2 | `/logs/header.bin` | `BytesFile`, immutable array | Read |

Create a provider object, then declare `file("/your/path", object)` inside
`filesystem(...)`. The table owns descriptors and borrows the provider, its
data and the path text. No directory creation or disk access happens.
Use literals for stable paths; put the provider and table in static storage
when they must survive for the entire application.

The custom `SettingsFile` implements `size/read/write` without a base class.
It delegates byte reads to `BytesFile` and checks a complete write chunk before
changing RAM. `final=true` acknowledges the last chunk; it does not trigger
Flash persistence. A real storage provider defines that operation itself.

Build with any supported C++20 compiler:

```sh
g++ -std=c++20 -Ilib examples/resources/Files.cpp -o resource-example
```

Run the resulting program. The generic resource runner also builds and runs
it on the host, and compiles it with the ARM compiler. It needs neither Qt nor
telemetry. Paths in that command are relative to the repository root.

The program demonstrates local access. To serve the same table through the
generic packet operations, pass `files.view()` to
`resource::protocol::process(files.view(), request, response)`. See the exact signature and packet layout in
[the protocol guide](../../lib/resource/protocol/README.md).
UART/TCP framing remains the caller's responsibility.

For telemetry, add the existing
[DescriptorFile and ValuesFile](../../lib/resource/telemetry/v3/README.md)
providers as additional entries in the same table. Generic and telemetry files
need no separate registries or different path syntax.
