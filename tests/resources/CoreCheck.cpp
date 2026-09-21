// Resource core and packet contracts. Authors: Ruslan Kovtun, codexAi (MIT).
#include <resource/FileSystem.hpp>
#include <resource/ChunkWriter.hpp>
#include <resource/protocol/Protocol.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using namespace resource;
int checks = 0;
#define CHECK(...)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(__VA_ARGS__))                                                                        \
        {                                                                                          \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__);                         \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

struct Memory
{
    std::array<std::byte, 10> bytes{};
    mutable unsigned reads = 0;
    mutable unsigned stats = 0;
    unsigned writes = 0;

    FileSize size() const noexcept
    {
        ++stats;
        return static_cast<FileSize>(bytes.size());
    }

    ReadResult read(Cursor cursor, Output out) const noexcept
    {
        ++reads;
        if (cursor > bytes.size())
        {
            return {Status::InvalidCursor, cursor};
        }
        auto count = std::min(out.size(), bytes.size() - static_cast<std::size_t>(cursor));
        if (count == 0 && cursor != bytes.size())
        {
            return {Status::BufferTooSmall, cursor};
        }
        if (count)
        {
            std::memcpy(out.data(), bytes.data() + cursor, count);
        }
        return {Status::Ok, cursor + count, static_cast<std::uint32_t>(count),
                cursor + count == bytes.size()};
    }

    WriteResult write(Cursor cursor, Input in, bool final) noexcept
    {
        ++writes;
        if (cursor > bytes.size())
        {
            return {Status::InvalidCursor, cursor};
        }
        auto count = std::min(in.size(), bytes.size() - static_cast<std::size_t>(cursor));
        if (count)
        {
            std::memmove(bytes.data() + cursor, in.data(), count);
        }
        return {Status::Ok, cursor + count, static_cast<std::uint32_t>(count),
                final && count == in.size()};
    }
};

struct ReadOnly
{
    FileSize size() const noexcept
    {
        return 0;
    }

    ReadResult read(Cursor c, Output) const noexcept
    {
        return c ? ReadResult{Status::InvalidCursor, c} : ReadResult{Status::Ok, 0, 0, true};
    }
};

struct WriteOnly
{
    FileSize size() const noexcept
    {
        return 0;
    }

    WriteResult write(Cursor c, Input in, bool final) noexcept
    {
        return {Status::Ok, c + 7, static_cast<std::uint32_t>(in.size()), final};
    }
};

struct Throwing
{
    FileSize size() const noexcept
    {
        return 0;
    }

    ReadResult read(Cursor, Output)
    {
        return {};
    }
};

struct WrongSize
{
    std::uint64_t size() const noexcept
    {
        return 0;
    }

    ReadResult read(Cursor, Output) const noexcept
    {
        return {};
    }
};

struct Misreport
{
    FileSize size() const noexcept
    {
        return 0;
    }

    ReadResult read(Cursor c, Output out) const noexcept
    {
        return {Status::Ok, c, static_cast<std::uint32_t>(out.size() + 1), true};
    }

    WriteResult write(Cursor c, Input in, bool) noexcept
    {
        return {Status::Ok, c, static_cast<std::uint32_t>(in.size() + 1), false};
    }
};

static_assert(!Provider<Throwing> && !Provider<WrongSize>);
static_assert(Provider<Memory> && Provider<const Memory> && !WritableProvider<const Memory>);
Memory memory;
const ReadOnly ro;
WriteOnly wo;
inline constexpr auto files =
    filesystem(file("/data.bin", memory), file("/ro", ro), file("/wo", wo));
static_assert(files.fileCount() == 3 && files.path(0) == "/data.bin");
static_assert(std::is_standard_layout_v<FileEntry> &&
              std::is_trivially_copy_constructible_v<FileEntry> &&
              std::is_trivially_destructible_v<FileEntry>);
constinit auto constantFs = filesystem(file("/value", memory));

template <class T>
void put(Output out, std::size_t p, T v)
{
    for (std::size_t i = 0; i < sizeof(T); ++i)
    {
        out[p + i] = std::byte((std::uint64_t(v) >> (8 * i)) & 255);
    }
}

std::uint64_t get(Input in, std::size_t p, std::size_t n)
{
    std::uint64_t r = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        r |= std::uint64_t(std::to_integer<unsigned>(in[p + i])) << (8 * i);
    }
    return r;
}

int main()
{
    std::array<std::byte, 128> out{};
    CHECK(files.path(3).empty());
    CHECK(files.stat(3).status == Status::InvalidFile);
    CHECK(has(files.stat(0).flags, FileFlag::Readable | FileFlag::Writable));
    CHECK(files.stat(1).flags == FileFlag::Readable && files.stat(2).flags == FileFlag::Writable);
    CHECK(files.read(2, 123, out).status == Status::NotReadable);
    CHECK(files.write(1, 123, {}).status == Status::NotWritable);
    CHECK(files.read(std::numeric_limits<FileIndex>::max(), 9, out).next == 9);
    CHECK(files.write(9, 7, {}).next == 7);
    CHECK(files.read(0, std::numeric_limits<Cursor>::max(), out).status == Status::InvalidCursor);
    CHECK(filesystem().fileCount() == 0);
    CHECK(FileSystemView{}.stat(0).status == Status::InvalidFile);
    const auto view = files.view();
    CHECK(view.fileCount() == 3 && constantFs.fileCount() == 1);
    ChunkWriter writer{Output{out}.first(3)};
    CHECK(!writer.writeAtomic(Input{out}.first(4)) && writer.empty());
    CHECK(writer.writePartial(Input{out}.first(4)) == 3 && writer.remaining() == 0);
    CHECK(writer.writeAtomic({}));
    ChunkWriter empty{{}};
    CHECK(empty.writePartial({}) == 0 && empty.writeAtomic({}));

    std::array<std::byte, 64> request{};
    using resource_protocol::process;
    request[0] = std::byte{1};
    const auto statCalls = memory.stats;
    auto r = process(view, Input{request}.first(9), Output{out}.first(23));
    CHECK(r.status == Status::Ok && r.written == 23 && get(out, 1, 8) == 1 &&
          out[9] == std::byte{0});
    CHECK(get(out, 12, 2) == 9 && std::memcmp(out.data() + 14, "/data.bin", 9) == 0);
    CHECK(memory.stats == statCalls);
    put(Output{request}, 1, Cursor{1});
    r = process(view, Input{request}.first(9), out);
    CHECK(r.status == Status::Ok && get(out, 1, 8) == 3 && out[9] == std::byte{1});
    put(Output{request}, 1, Cursor{3});
    CHECK(process(view, Input{request}.first(9), out).written == 12 && out[9] == std::byte{1});
    put(Output{request}, 1, Cursor{4});
    CHECK(process(view, Input{request}.first(9), out).status == Status::InvalidCursor &&
          get(out, 1, 8) == 4);
    put(Output{request}, 1, Cursor{0});
    CHECK(process(view, Input{request}.first(9), Output{out}.first(22)).status ==
              Status::BufferTooSmall &&
          get(out, 1, 8) == 0);
    request[0] = std::byte{2};
    CHECK(process(view, Input{request}.first(5), out).written == 6 && get(out, 1, 4) == 10 &&
          out[5] == std::byte{3});
    put(Output{request}, 1, std::uint32_t{0xffffffffu});
    CHECK(process(view, Input{request}.first(5), out).status == Status::InvalidFile);
    put(Output{request}, 1, std::uint32_t{0});
    request[0] = std::byte{4};
    put(Output{request}, 5, Cursor{0});
    request[13] = std::byte{1};
    put(Output{request}, 14, std::uint16_t{3});
    request[16] = std::byte{10};
    request[17] = std::byte{20};
    request[18] = std::byte{30};
    auto writes = memory.writes;
    CHECK(process(view, Input{request}.first(19), Output{out}.first(13)).status ==
              Status::BufferTooSmall &&
          memory.writes == writes);
    r = process(view, Input{request}.first(19), out);
    CHECK(r.status == Status::Ok && r.written == 14 && get(out, 1, 8) == 3 && get(out, 9, 4) == 3 &&
          out[13] == std::byte{1});
    (void)process(view, Input{request}.first(19), out);
    CHECK(memory.writes == writes + 2 && memory.bytes[1] == std::byte{20});
    request[13] = std::byte{2};
    CHECK(process(view, Input{request}.first(19), out).status == Status::InvalidData &&
          memory.writes == writes + 2);
    request[13] = std::byte{1};
    CHECK(process(view, Input{request}.first(18), out).status == Status::InvalidData);
    CHECK(process(view, Input{request}.first(20), out).status == Status::InvalidData);
    request[0] = std::byte{3};
    r = process(view, Input{request}.first(13), out);
    CHECK(r.status == Status::Ok && get(out, 10, 2) == 10 && out[12] == std::byte{10});
    CHECK(process(view, Input{request}.first(13), Output{out}.first(12)).status ==
          Status::BufferTooSmall);
    // Equal begin/end cursors on an empty file are valid EOF, even with no
    // READ payload capacity. Provider count mistakes cannot corrupt envelopes.
    put(Output{request}, 1, std::uint32_t{1});
    CHECK(process(view, Input{request}.first(13), Output{out}.first(12)).status == Status::Ok &&
          out[9] == std::byte{1});
    Misreport wrong;
    const auto wrongFs = filesystem(file("/wrong", wrong));
    put(Output{request}, 1, std::uint32_t{0});
    CHECK(process(wrongFs.view(), Input{request}.first(13), out).status == Status::InternalError);
    request[0] = std::byte{4};
    CHECK(process(wrongFs.view(), Input{request}.first(19), out).status == Status::InternalError);
    // In-place WRITE parses its input before the response overwrites it.
    request[0] = std::byte{4};
    CHECK(process(view, Input{request}.first(19), request).status == Status::Ok &&
          memory.bytes[0] == std::byte{10});
    // A size larger than the wire payload is capped before reaching a provider.
    std::vector<std::byte> large(70000, std::byte{0});
    request.fill(std::byte{0});
    request[0] = std::byte{3};
    CHECK(process(wrongFs.view(), Input{request}.first(13), large).status == Status::InternalError);
    for (unsigned op = 1; op <= 4; ++op)
    {
        request.fill(std::byte{0});
        request[0] = std::byte(op);
        const std::size_t fixed = op == 1 ? 9 : op == 2 ? 5 : op == 3 ? 13 : 16;
        for (std::size_t n = 0; n < fixed; ++n)
        {
            CHECK(process(view, Input{request}.first(n), out).status == Status::InvalidData);
        }
    }
    // Arbitrary complete packets and every small reply extent, with guard bytes.
    // Run under sanitizers too: malformed lengths must never reach a span read.
    std::uint32_t seed = 91;
    for (unsigned trial = 0; trial < 4000; ++trial)
    {
        for (auto& b : request)
        {
            seed = seed * 1664525u + 1013904223u;
            b = std::byte(seed >> 24);
        }
        if (trial % 2 == 0)
        {
            request[0] = std::byte(1 + trial % 4);
        }
        const auto inSize = trial % 65, outSize = trial % 32;
        out.fill(std::byte{0xa5});
        r = process(view, Input{request}.first(inSize), Output{out}.subspan(1, outSize));
        CHECK(r.written <= outSize && out[0] == std::byte{0xa5} &&
              out[1 + outSize] == std::byte{0xa5});
    }
    std::printf("Resource core/protocol: %d checks passed\n", checks);
}
