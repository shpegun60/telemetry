/* Whole-token resource, cursor, scratch and protocol checks. MIT. */
#include "Fixture.hpp"
#include <resource/protocol/Protocol.hpp>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace fixture;
using resource::Status;
using Bytes = std::vector<std::byte>;

template <class T>
void put(std::byte* out, T value)
{
    for (std::size_t i = 0; i < sizeof(T); ++i) out[i] = std::byte(value >> (i * 8));
}

Bytes expected()
{
    Bytes out(values.size());
    out[0] = std::byte{'T'}; out[1] = std::byte{'V'}; out[2] = std::byte{'L'}; out[3] = std::byte{'3'};
    put(out.data() + 4, std::uint16_t{3});
    put(out.data() + 8, std::uint32_t{10});
    put(out.data() + 12, values.size());
    put(out.data() + 16, descriptor.fingerprint());
    put(out.data() + 25, counter);
    out[30] = std::byte{1};
    put(out.data() + 32, std::uint64_t{0x8000000000000000ULL});
    put(out.data() + 41, std::uint16_t{0xfffe});
    put(out.data() + 44, std::uint16_t{0x1234});
    put(out.data() + 46, std::uint32_t{0x3fc00000});
    put(out.data() + 52, std::uint16_t{1});
    put(out.data() + 54, std::uint16_t{256});
    put(out.data() + 56, std::uint16_t{65535});
    out[60] = out[65] = std::byte{1}; // Unavailable: payload remains all zero.
    return out;
}

unsigned cursorMatrix()
{
    const auto golden = expected();
    unsigned checked = 0;
    for (std::uint64_t cursor = 0; cursor <= values.size() + 1; ++cursor) {
        for (unsigned capacity = 0; capacity <= values.size() + 2; ++capacity) {
            calls.fill(0);
            Bytes out(capacity + 2, std::byte{0xcd});
            const auto result = values.read(cursor, {out.data() + 1, capacity});
            const bool valid = cursor < 24 || std::find(offsets.begin(), offsets.end(), cursor) != offsets.end();
            std::uint32_t used = 0;
            std::array<unsigned, 12> wanted{};
            if (valid && cursor < values.size()) {
                if (cursor < 24) used = static_cast<std::uint32_t>(std::min<std::uint64_t>(24 - cursor, capacity));
                for (std::size_t i = 0; i + 1 < offsets.size(); ++i) {
                    if (offsets[i] != cursor + used) continue;
                    const auto token = offsets[i + 1] - offsets[i];
                    if (token > capacity - used) break;
                    used += token;
                    if (i < 8) wanted[i] = 1;
                }
            }
            const auto status = !valid ? Status::InvalidCursor :
                (cursor == values.size() || used ? Status::Ok : Status::BufferTooSmall);
            assert(result.status == status);
            assert(result.written == used && result.next == cursor + used);
            assert(result.eof == (status == Status::Ok && cursor + used == values.size()));
            assert(calls == wanted && workspace.used() == 0);
            assert(std::equal(out.begin() + 1, out.begin() + 1 + used, golden.begin() + std::min<std::uint64_t>(cursor, golden.size())));
            assert(out.front() == std::byte{0xcd});
            assert(std::all_of(out.begin() + 1 + used, out.end(), [](auto b) { return b == std::byte{0xcd}; }));
            ++checked;
        }
    }
    const auto invalid = values.read(UINT64_MAX, {});
    assert(invalid.status == Status::InvalidCursor && invalid.next == UINT64_MAX);
    // Every fixed chunk capacity that can hold the largest token completes.
    for (unsigned capacity = values.maxTokenSize(); capacity < 90; ++capacity) {
        Bytes combined;
        Bytes buffer(capacity);
        std::uint64_t cursor = 0;
        calls.fill(0);
        do {
            const auto result = values.read(cursor, buffer);
            assert(result.status == Status::Ok && result.next > cursor);
            combined.insert(combined.end(), buffer.begin(), buffer.begin() + result.written);
            cursor = result.next;
            if (result.eof) break;
        } while (true);
        assert(combined == golden);
        for (unsigned i = 0; i < 8; ++i) assert(calls[i] == 1);
    }
    return checked;
}

void descriptorAndSlots()
{
    for (std::size_t start = 0; start <= packed.size(); ++start) {
        std::array<std::byte, 27> a{}, b{};
        const auto x = packedFile.read(start, a), y = streamedFile.read(start, b);
        assert(x.status == y.status && x.next == y.next && x.written == y.written && x.eof == y.eof && a == b);
    }
    assert(packedFile.read(UINT64_MAX, {}).status == Status::InvalidCursor);
    assert(streamedFile.read(UINT64_MAX, {}).status == Status::InvalidCursor);
    assert(packedFile.read(0, {}).status == Status::BufferTooSmall);
    const auto savedSize = values.size();
    const auto savedHash = values.fingerprint();
    functionSlot.bind(&readSlot);
    ownerSlot.bind(owner);
    std::array<std::byte, 13> out{};
    calls.fill(0);
    const auto read = values.read(60, out);
    assert(read.status == Status::Ok && read.eof && read.written == 13);
    assert(out[0] == std::byte{0} && out[1] == std::byte{42} && out[5] == std::byte{0});
    assert(calls[8] == 1 && calls[9] == 1);
    assert(values.size() == savedSize && values.fingerprint() == savedHash);
    functionSlot.reset(); ownerSlot.reset();
    out.fill(std::byte{0xcd});
    assert(values.read(60, out).eof);
    for (std::size_t i = 0; i < out.size(); ++i)
        assert(out[i] == ((i == 0 || i == 5) ? std::byte{1} : std::byte{0}));
    // A copy owns its own offset table, borrowing the same live sources.
    auto copy = values;
    assert(copy.read(60, out).eof);
    std::array<std::byte, 24> header{};
    assert(emptyValues.read(0, header).eof);
    assert(emptyValues.read(24, {}).eof);
    assert(emptyValues.read(23, {}).status == Status::BufferTooSmall);
    const auto previous = counter;
    counter = UINT32_MAX;
    std::array<std::byte, 5> changed{};
    assert(values.read(24, changed).written == 5);
    assert(changed[0] == std::byte{0} && changed[1] == std::byte{0xff} && changed[4] == std::byte{0xff});
    assert(values.size() == savedSize && values.fingerprint() == savedHash);
    counter = previous;
}

void scratch()
{
    Bytes out(bigValues.size(), std::byte{0xcc});
    calls.fill(0);
    const auto tooShort = bigValues.read(29, {out.data(), 4096});
    assert(tooShort.status == Status::BufferTooSmall && calls[10] == 0);
    const auto read = bigValues.read(29, out);
    assert(read.status == Status::Ok && read.eof && read.written == 4097);
    assert(calls[10] == 1 && bigWorkspace.used() == 0 && out[1] == std::byte{4});
    // Exact-size storage at every possible starting alignment is accepted when
    // the published worst-case scratch capacity is supplied.
    std::array<std::byte, 4112> storage{};
    for (unsigned offset = 0; offset < alignof(Big); ++offset) {
        ts::Workspace ws{{storage.data() + offset, bigValues.requiredWorkspace()}};
        rs::ValuesFile file{bigDescriptor, ws};
        assert(file.read(29, out).eof && ws.used() == 0);
    }
    ts::Workspace tiny{{storage.data(), 4095}};
    rs::ValuesFile bad{bigDescriptor, tiny};
    calls.fill(0);
    out.assign(out.size(), std::byte{0xcc});
    auto r = bad.read(29, out);
    assert(r.status == Status::InternalError && r.next == 29 && calls[10] == 0);
    assert(std::all_of(out.begin(), out.end(), [](auto b) { return b == std::byte{0xcc}; }));
    r = bad.read(0, out);
    assert(r.status == Status::Ok && r.written == 29 && !r.eof && calls[0] == 1 && calls[10] == 0);
    // A live caller lease keeps its storage and used position through failure.
    {
        auto lease = bigWorkspace.reserve<Big>();
        assert(lease.valid());
        const auto used = bigWorkspace.used();
        assert(bigValues.read(29, out).status == Status::InternalError);
        assert(bigWorkspace.used() == used);
    }
    assert(bigWorkspace.used() == 0);
    bigStorage.fill(std::byte{0xcb});
    assert(bigValues.read(0, bigStorage).status == Status::InvalidData);
    assert(std::all_of(bigStorage.begin(), bigStorage.end(), [](auto b) { return b == std::byte{0xcb}; }));
    // All-local fields never touch the unused Workspace, even when it aliases output.
    if constexpr (values.requiredWorkspace() == 0) {
        ts::Workspace aliasWorkspace{out};
        rs::ValuesFile alias{descriptor, aliasWorkspace};
        assert(alias.read(0, out).eof && aliasWorkspace.used() == 0);
    }
}

void protocol()
{
    std::array<std::byte, 13> request{};
    request[0] = std::byte{3}; put(request.data() + 1, std::uint32_t{1});
    std::array<std::byte, 85> reply{};
    auto r = resource_protocol::process(fs.view(), request, reply);
    assert(r.status == Status::Ok && r.written == reply.size());
    assert(reply[9] == std::byte{1} && reply[10] == std::byte{73});
    assert(std::equal(reply.begin() + 12, reply.end(), expected().begin()));
    put(request.data() + 5, std::uint64_t{25});
    r = resource_protocol::process(fs.view(), request, reply);
    assert(r.status == Status::InvalidCursor && r.written == 12);
    put(request.data() + 5, std::uint64_t{24});
    r = resource_protocol::process(fs.view(), request, {reply.data(), 16});
    assert(r.status == Status::BufferTooSmall && r.written == 12 && reply[1] == std::byte{24});
    assert(fs.view().stat(1).size == values.size());
    assert(fs.view().stat(1).flags == resource::FileFlag::Readable);
    assert(fs.view().write(1, 0, {}, true).status == Status::NotWritable);
}

int main(int argc, char** argv)
{
    assert(calls == decltype(calls){}); // construction and size queries did not read live values.
    const auto checked = cursorMatrix();
    descriptorAndSlots(); scratch(); protocol();
    if (argc == 3) {
        Bytes bytes(values.size());
        assert(values.read(0, bytes).eof);
        std::ofstream(argv[1], std::ios::binary).write(reinterpret_cast<const char*>(packed.data()), packed.size());
        std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    std::printf("Stage 10 cursor/capacity cases: %u; descriptor/slot/workspace/protocol checks passed\n", checked);
}
