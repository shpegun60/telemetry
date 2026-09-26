// Regression promoted from tests/review/resource/CursorFuzz.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review harness (resource slice): cursor/capacity properties and packet fuzzing.
// For every block offset x capacity: output equals the exact slice of the complete
// file and `next` maps to (start + written). Random cursors: error replies keep the
// cursor and commit nothing. Random packets through process(): guard bytes intact.
#include <telemetry/Telemetry.h>
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <resource/telemetry/TelemetryFiles.hpp>
#include <resource/telemetry/detail/BlockStream.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#define CHECK(...)                                                                           \
    do                                                                                       \
    {                                                                                        \
        if (!(__VA_ARGS__))                                                                  \
        {                                                                                    \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__);                   \
            std::abort();                                                                    \
        }                                                                                    \
    } while (false)

using namespace telemetry;
using Bytes = std::vector<std::byte>;
namespace d = telemetry_resource::detail;

enum class Mode : std::uint16_t { Off, Auto, Manual };
enum class Big : std::uint64_t { Zero = 0, Top = UINT64_MAX };
float volts() noexcept { return 230.5f; }
Mode mode() noexcept { return Mode::Manual; }
Big big() noexcept { return Big::Top; }
Scalar missing() noexcept { return Scalar::null(); }
WriteResult setVolts(float) noexcept { return WriteResult::Applied; }
CommandResult configure(float, Mode, Big) noexcept { return CommandResult::Executed; }
CommandResult zero() noexcept { return CommandResult::Executed; }

constexpr FieldTable rows{field<&volts, &setVolts>("V\"x", "V\n", limits(1.0f, -10.0f, 300.0f)),
                          field<&mode>("Mode", ""),
                          field<&big>("Big", "", enumSpec<Big::Zero, Big::Top>())};
const Field loose[] = {{"s", "", ScalarType::S16, &missing}, {}, {"b", "u", ScalarType::Bool}};
const Catalog catalogs[] = {{"first", rows.data(), rows.size()}, {"empty", nullptr, 0},
                            {"loose", loose}, {"empty2", nullptr, 0}};
constexpr CommandTable commandRows{
    command<&configure>("Configure", arg<0>("V", "V", 1.0f, -10.0f, 300.0f), arg<1>("M", "", Mode::Auto)),
    command<&zero>(""), reservedCommand()};
const CommandCatalog commandCatalogs[] = {{"c", commandRows.data(), commandRows.size()},
                                          {"none", nullptr, 0},
                                          {"c2", commandRows.data(), commandRows.size()}};

template <class File>
Bytes whole(const File& f)
{
    Bytes all(f.size());
    const auto r = f.read(0, all);
    CHECK(r.status == resource::Status::Ok && r.eof && r.written == all.size());
    return all;
}

// cursor -> absolute offset for every valid (block, offset), rebuilt from wire records.
std::map<resource::Cursor, std::size_t> metadataCursors(const Bytes& all, bool schema)
{
    std::map<resource::Cursor, std::size_t> map;
    std::vector<std::pair<resource::Cursor, std::size_t>> blocks{{0, 0}};
    std::size_t at = 44;
    auto u32 = [&](std::size_t p) {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t(std::to_integer<unsigned>(all[p + i])) << (8 * i);
        return v;
    };
    while (at < all.size())
    {
        const auto type = std::to_integer<unsigned>(all[at]);
        const auto size = u32(at + 4);
        if (type == (schema ? 2u : 1u)) blocks.push_back({d::pack(d::BlockKind::Catalog, u32(at + 8)), at});
        else if (type == (schema ? 3u : 2u)) blocks.push_back({d::pack(d::BlockKind::Entry, u32(at + 16)), at});
        at += 8 + size;
    }
    CHECK(at == all.size());
    blocks.push_back({d::endCursor, all.size()});
    for (std::size_t n = 0; n + 1 < blocks.size(); ++n)
        for (std::size_t o = 0; o <= blocks[n + 1].second - blocks[n].second; ++o)
            map[blocks[n].first | o] = blocks[n].second + o;
    map[d::endCursor] = all.size();
    return map;
}

std::map<resource::Cursor, std::size_t> valueCursors(const telemetry_resource::ValuesFile&,
                                                     const CatalogIndex& index)
{
    std::map<resource::Cursor, std::size_t> map;
    for (unsigned o = 0; o <= 20; ++o) map[o] = o;
    std::size_t at = 20;
    for (auto c : index.catalogs())
        for (auto e : c.fields())
        {
            map[d::pack(d::BlockKind::Entry, e.id())] = at;
            at += 1 + telemetry_resource::payloadSize(telemetry_resource::toWireType(e.field().readType));
        }
    map[d::endCursor] = at;
    return map;
}

template <class File>
void exhaustive(const File& f, const Bytes& all, const std::map<resource::Cursor, std::size_t>& map,
                bool atomicTokens)
{
    Bytes out(64 + 2);
    for (const auto& [cursor, abs] : map)
        for (std::size_t cap = 0; cap <= 40; ++cap)
        {
            std::fill(out.begin(), out.end(), std::byte{0xa5});
            const auto r = f.read(cursor, {out.data() + 1, cap});
            CHECK(out[0] == std::byte{0xa5} && out[cap + 1] == std::byte{0xa5});
            if (r.status == resource::Status::BufferTooSmall)
            {
                CHECK(r.written == 0 && r.next == cursor && !r.eof);
                CHECK(atomicTokens ? cap < 9 : cap == 0);
                continue;
            }
            CHECK(r.status == resource::Status::Ok && r.written <= cap);
            CHECK(std::memcmp(out.data() + 1, all.data() + abs, r.written) == 0);
            CHECK(r.written != 0 || r.next != cursor || r.eof);
            if (r.eof)
                CHECK(abs + r.written == all.size() && r.next == d::endCursor);
            else
            {
                const auto next = map.find(r.next);
                CHECK(next != map.end() && next->second == abs + r.written);
            }
        }
}

template <class File>
void randomCursors(const File& f, const std::map<resource::Cursor, std::size_t>& map, std::uint64_t seed)
{
    Bytes out(33);
    for (unsigned i = 0; i < 200000; ++i)
    {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        resource::Cursor c = seed;
        switch (i % 4)
        {
            case 0: break;                                                  // fully random
            case 1: c = d::pack(static_cast<d::BlockKind>(seed & 3), (seed >> 8) & 0x3ffff, (seed >> 40) & 0x3f); break;
            case 2: c = d::pack(d::BlockKind::Entry, std::uint32_t(seed >> 20), 0); break;
            default: { auto it = map.begin(); std::advance(it, seed % map.size()); c = it->first + ((seed >> 32) % 3); }
        }
        const auto cap = (seed >> 50) % 33;
        const auto r = f.read(c, {out.data(), cap});
        const bool valid = map.count(c) != 0;
        if (!valid)
            CHECK(r.status == resource::Status::InvalidCursor && r.next == c && r.written == 0 && !r.eof);
        else
            CHECK(r.status == resource::Status::Ok || r.status == resource::Status::BufferTooSmall);
    }
}

int main()
{
    const CatalogIndex index{catalogs};
    const CommandCatalogIndex commandIndex{commandCatalogs};
    telemetry_resource::SchemaFile schema{index};
    telemetry_resource::CommandsFile commands{commandIndex};
    telemetry_resource::ValuesFile values{schema};
    CHECK(schema.size() && commands.size() && values.size());
    const auto sb = whole(schema), cb = whole(commands), vb = whole(values);
    const auto sm = metadataCursors(sb, true), cm = metadataCursors(cb, false);
    const auto vm = valueCursors(values, index);
    exhaustive(schema, sb, sm, false);
    exhaustive(commands, cb, cm, false);
    exhaustive(values, vb, vm, true);
    randomCursors(schema, sm, 0x1234567);
    randomCursors(commands, cm, 0x7654321);
    randomCursors(values, vm, 0x55aa55aa);

    // Random packets (all four ops, random lengths) through process() with guards.
    const auto fs = resource::filesystem(resource::file("/s", schema), resource::file("/c", commands),
                                         resource::file("/v", values));
    std::uint64_t seed = 99;
    Bytes request(40), response(80);
    for (unsigned i = 0; i < 300000; ++i)
    {
        for (auto& b : request) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; b = std::byte(seed); }
        request[0] = std::byte(1 + seed % 5);
        if (request[0] == std::byte{3} && (seed & 1)) // aim READ at valid indices/cursors
        {
            request[1] = std::byte(seed % 4); request[2] = request[3] = request[4] = std::byte{0};
            const auto& m = (seed % 3 == 0) ? sm : (seed % 3 == 1) ? cm : vm;
            auto it = m.begin(); std::advance(it, (seed >> 9) % m.size());
            for (int k = 0; k < 8; ++k) request[5 + k] = std::byte((it->first >> (8 * k)) & 0xff);
        }
        const std::size_t inSize = (seed >> 20) % 41, outSize = (seed >> 30) % 70;
        std::fill(response.begin(), response.end(), std::byte{0x5a});
        const auto r = resource_protocol::process(fs.view(), {request.data(), inSize}, {response.data() + 1, outSize});
        CHECK(r.written <= outSize && response[0] == std::byte{0x5a} && response[outSize + 1] == std::byte{0x5a});
        if (r.written >= 12 && (request[0] == std::byte{1} || request[0] == std::byte{3}) && inSize == (request[0] == std::byte{1} ? 9u : 13u))
        {
            const unsigned size = std::to_integer<unsigned>(response[11]) | std::to_integer<unsigned>(response[12]) << 8;
            CHECK(size + 12 == r.written);
        }
    }
    std::printf("cursor fuzz: schema %zu/%zu, commands %zu/%zu, values %zu/%zu cursors; all properties held\n",
                sm.size(), sb.size(), cm.size(), cb.size(), vm.size(), vb.size());
}
