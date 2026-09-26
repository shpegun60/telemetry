// Review repro (resource slice): LIST when a representable path is followed by
// one that no v1 packet can carry (> 65533 bytes).
// README (protocol): "With some entries emitted, a full buffer returns Ok and the
// first unreturned index." and "Longer paths return InvalidData".
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

using namespace resource;

struct Empty
{
    FileSize size() const noexcept { return 0; }
    ReadResult read(Cursor c, Output) const noexcept { return {Status::Ok, c, 0, true}; }
};

static std::uint64_t get(Input in, std::size_t p, std::size_t n)
{
    std::uint64_t r = 0;
    for (std::size_t i = 0; i < n; ++i)
        r |= std::uint64_t(std::to_integer<unsigned>(in[p + i])) << (8 * i);
    return r;
}

int main()
{
    Empty provider;
    std::string longPath(65534, 'x');
    longPath.front() = '/';
    const auto fs = filesystem(file("/a", provider), file(std::string_view{longPath}, provider));

    std::array<std::byte, 9> list{};
    list[0] = std::byte{1};
    int failures = 0;
    for (std::size_t payload : {4u, 5u, 100u, 65535u})
    {
        // cursor 0: entry 0 ("/a", 4 bytes on the wire) always fits in these payloads.
        std::vector<std::byte> reply(12 + payload);
        const auto r = resource_protocol::process(fs.view(), list, reply);
        std::printf("payload %5zu: status=%u written=%zu next=%llu eof=%u dataSize=%llu\n",
                    payload, unsigned(r.status), r.written,
                    (unsigned long long)get(reply, 1, 8), unsigned(reply[9]),
                    (unsigned long long)get(reply, 10, 2));
        if (r.status != Status::Ok)
            ++failures;
    }
    // Entry 0 on its own can never be listed: cursor 0 always reports InvalidData
    // for any buffer that holds entry 0.
    std::printf(failures ? "DEFECT: entry 0 (\"/a\") is unlistable; %d of 4 page sizes fail\n"
                         : "no defect\n",
                failures);
    return failures ? 1 : 0;
}
