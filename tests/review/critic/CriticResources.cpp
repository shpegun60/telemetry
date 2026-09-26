// Critic trial consumer (C++20 part): the binary resource files and the packet protocol,
// assembled from lib/resource/README.md and lib/resource/telemetry/README.md only.
#include "CriticDevice.h"

#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <resource/telemetry/TelemetryFiles.hpp>

#include <cstdarg>
#include <cstdio>

namespace critic {
namespace {
telemetry_resource::SchemaFile schema{fields.index()};
telemetry_resource::CommandsFile commandFile{commands.index()};
telemetry_resource::ValuesFile values{schema};

constinit const auto files = resource::filesystem(
    resource::file("/telemetry/schema.bin", schema),
    resource::file("/telemetry/commands.bin", commandFile),
    resource::file("/telemetry/values.bin", values));

std::byte fileBytes[2048];

struct Text {
    char* out;
    std::size_t capacity;
    std::size_t used = 0;
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    void add(const char* format, ...) noexcept
    {
        if (used >= capacity) return;
        std::va_list args;
        va_start(args, format);
        const int n = std::vsnprintf(out + used, capacity - used, format, args);
        va_end(args);
        if (n > 0) used += static_cast<std::size_t>(n) < capacity - used ? static_cast<std::size_t>(n) : capacity - used - 1;
    }
};

// Reads one file completely through the provider API in deliberately small chunks.
std::size_t readWhole(resource::FileIndex index, std::size_t chunk, unsigned& calls) noexcept
{
    resource::Cursor cursor = 0;
    std::size_t total = 0;
    calls = 0;
    for (;;) {
        const std::size_t room = sizeof(fileBytes) - total;
        const std::size_t ask = room < chunk ? room : chunk;
        const auto r = files.read(index, cursor, resource::Output{fileBytes + total, ask});
        ++calls;
        if (r.status != resource::Status::Ok) return 0;
        total += r.written;
        cursor = r.next;
        if (r.eof) return total;
        if (total == sizeof(fileBytes)) return 0;
    }
}
} // namespace

// Host only: writes the three files into an existing directory, for the Python decoder trial.
int saveResources(const char* directory) noexcept
{
    for (resource::FileIndex i = 0; i < files.fileCount(); ++i) {
        unsigned calls = 0;
        const std::size_t got = readWhole(i, 64, calls);
        const auto path = files.path(i);
        const auto slash = path.rfind('/');
        char name[256];
        std::snprintf(name, sizeof(name), "%s/%.*s", directory, static_cast<int>(path.size() - slash - 1),
                      path.data() + slash + 1);
        std::FILE* f = std::fopen(name, "wb");
        if (!f) return 1;
        std::fwrite(fileBytes, 1, got, f);
        std::fclose(f);
    }
    return 0;
}

std::size_t dumpResources(char* text, std::size_t capacity) noexcept
{
    Text t{text, capacity};
    for (resource::FileIndex i = 0; i < files.fileCount(); ++i) {
        const auto st = files.stat(i);
        unsigned calls = 0;
        const std::size_t got = readWhole(i, 16, calls);
        t.add("%.*s: stat.size=%u read=%u bytes in %u calls of <=16 bytes; head:",
              static_cast<int>(files.path(i).size()), files.path(i).data(),
              static_cast<unsigned>(st.size), static_cast<unsigned>(got), calls);
        for (std::size_t b = 0; b < got && b < 24; ++b) t.add(" %02x", static_cast<unsigned>(fileBytes[b]));
        t.add("\n");
    }

    // The same values file through one protocol READ packet: u8 op, u32 index, u64 cursor.
    std::byte request[13] = {std::byte{3}, std::byte{2}};
    std::byte response[256];
    const auto reply = resource_protocol::process(files.view(), request, response);
    t.add("protocol READ values.bin -> status=%u written=%u\n",
          static_cast<unsigned>(reply.status), static_cast<unsigned>(reply.written));
    return t.used;
}

} // namespace critic
