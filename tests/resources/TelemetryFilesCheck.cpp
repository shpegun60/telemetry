// Independent binary decoding, goldens, boundaries, fingerprints and live getters (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include "TestSupport.hpp"
#include "Golden.hpp"
#include <algorithm>
#include <telemetry/Telemetry.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include <resource/telemetry/BinaryFormat.hpp>
#include <resource/protocol/Protocol.hpp>
#include <resource/FileSystem.hpp>
#include <filesystem>
using namespace telemetry;
unsigned getters = 0, invoked = 0;
float voltage = 1.0f;

float readVoltage() noexcept
{
    ++getters;
    return voltage;
}

WriteResult setVoltage(float value) noexcept
{
    voltage = value;
    return WriteResult::Applied;
}
enum class Mode : std::uint16_t
{
    Off = 0,
    Auto = 1,
    Manual = 2
};
enum class Large : std::uint64_t
{
    Min = 0,
    Max = std::numeric_limits<std::uint64_t>::max()
};

namespace magic_enum::customize
{
template <>
constexpr customize_t enum_name(Mode value) noexcept
{
    if (value == Mode::Manual)
    {
        return std::string_view{"Man\0ual", 7};
    }
    return default_tag;
}
} // namespace magic_enum::customize

Mode readMode() noexcept
{
    return Mode::Auto;
}

Large readLarge() noexcept
{
    return Large::Max;
}

CommandResult configure(float, Mode, std::uint64_t) noexcept
{
    ++invoked;
    return CommandResult::Executed;
}

constexpr FieldTable fieldTable{
    field<&readVoltage, &setVoltage>("U\"a\xc3\xa9", "V\n", limits(1.0f, -10.0f, 300.0f))
        .withFlags(FieldFlag::Persistent),
    field<&readMode>("Mode", ""),
    field<&readLarge>("Large", "", enumSpec<Large::Min, Large::Max>())};
constexpr FieldCatalogTable fields{group("me\"ter", fieldTable)};
constexpr CommandTable commandTable{command<&configure>(
    "Con\nfigure", arg<0>("Voltage", "V", 1.0f, -10.0f, 300.0f), arg<1>("Mode", "", Mode::Auto))};
constexpr CommandCatalogTable commands{group("meter", commandTable)};

Scalar values[] = {Scalar::fromF32(1.0f),
                   Scalar::fromF64(-0.0),
                   Scalar::fromU8(255),
                   Scalar::fromU16(0x1234),
                   Scalar::fromU32(0x12345678),
                   Scalar::fromU64(0xffffffffffffffffull),
                   Scalar::fromS8(-1),
                   Scalar::fromS16(-32768),
                   Scalar::fromS32(std::numeric_limits<std::int32_t>::min()),
                   Scalar::fromS64(std::numeric_limits<std::int64_t>::min()),
                   Scalar::fromBool(true),
                   Scalar::null()};

template <std::size_t N>
Scalar readValue() noexcept
{
    ++getters;
    return values[N];
}

const Field scalarRows[] = {{"f32", "", ScalarType::F32, &readValue<0>},
                            {"f64", "", ScalarType::F64, &readValue<1>},
                            {"u8", "", ScalarType::U8, &readValue<2>},
                            {"u16", "", ScalarType::U16, &readValue<3>},
                            {"u32", "", ScalarType::U32, &readValue<4>},
                            {"u64", "", ScalarType::U64, &readValue<5>},
                            {"s8", "", ScalarType::S8, &readValue<6>},
                            {"s16", "", ScalarType::S16, &readValue<7>},
                            {"s32", "", ScalarType::S32, &readValue<8>},
                            {"s64", "", ScalarType::S64, &readValue<9>},
                            {"bool", "", ScalarType::Bool, &readValue<10>},
                            {"missing", "", ScalarType::F32, &readValue<11>},
                            {}};
const Catalog scalarCatalogs[] = {{"all", scalarRows}, {"empty", nullptr, 0}};

// Expected wire tags are deliberately independent of the production mapping.
unsigned tag(ScalarType t)
{
    switch (t)
    {
        case ScalarType::Null:
            return 0;
        case ScalarType::Bool:
            return 1;
        case ScalarType::U8:
            return 2;
        case ScalarType::U16:
            return 3;
        case ScalarType::U32:
            return 4;
        case ScalarType::U64:
            return 5;
        case ScalarType::S8:
            return 6;
        case ScalarType::S16:
            return 7;
        case ScalarType::S32:
            return 8;
        case ScalarType::S64:
            return 9;
        case ScalarType::F32:
            return 10;
        case ScalarType::F64:
            return 11;
    }
    CHECK(false);
    return 0;
}

unsigned width(unsigned type)
{
    constexpr unsigned sizes[]{0, 1, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8};
    CHECK(type < 12);
    return sizes[type];
}

std::uint64_t bits(const Scalar& s)
{
    return s.visit(
        [](auto v) -> std::uint64_t
        {
            using T = decltype(v);
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return 0;
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                return std::bit_cast<std::uint32_t>(v);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return std::bit_cast<std::uint64_t>(v);
            }
            else
            {
                return static_cast<std::uint64_t>(v);
            }
        });
}

void scalar(Reader& r, const Scalar& s)
{
    const auto type = tag(s.type()), bytes = width(type);
    CHECK(r.u8() == type && r.u8() == (type == 0 ? 0 : 1) && r.u8() == bytes);
    const auto mask = bytes == 8 ? UINT64_MAX : (UINT64_C(1) << (bytes * 8)) - 1;
    CHECK(r.number(bytes) == (bits(s) & mask));
}

void limits(Reader& r, const FieldType& t)
{
    scalar(r, t.minimum());
    scalar(r, t.maximum());
    scalar(r, t.defaultValue());
}

template <class Visit>
void enums(const FieldType& t, Visit&& visitor)
{
    if (!t.hasEnum())
    {
        return;
    }
    using V = std::remove_reference_t<Visit>;
    CHECK(t.describeEnum(&visitor,
                         [](void* raw, const Scalar& s, std::string_view n) noexcept
                         {
                             (*static_cast<V*>(raw))(s, n);
                             return true;
                         }));
}

void verifySchema(const Bytes& bytes, const CatalogIndex& index, std::uint64_t fingerprint)
{
    Reader r{bytes};
    CHECK(r.raw(4) == "TSCH" && r.u16() == 2 && r.u16() == 0);
    CHECK(r.u32() == 44 && r.u32() == bytes.size() && r.number(8) == fingerprint);
    const auto records = r.u32(), catalogs = r.u32(), fieldsCount = r.u32(), enumsCount = r.u32(),
               flags = r.u32();
    CHECK(catalogs == index.size() && flags == 1);
    auto flag = r.record(1);
    CHECK(flag.u32() == 1 && flag.string() == "Persistent");
    flag.done();
    unsigned count = 1, fc = 0, ec = 0;
    for (auto c : index.catalogs())
    {
        auto cat = r.record(2);
        ++count;
        CHECK(cat.u32() == c.index() && cat.u32() == c.fields().size() && cat.string() == c.name());
        cat.done();
        for (auto e : c.fields())
        {
            const auto& f = e.field();
            auto fr = r.record(3);
            ++count;
            ++fc;
            CHECK(fr.u32() == c.index() && fr.u32() == e.index() && fr.u32() == e.id() &&
                  fr.u32() == f.flags().value());
            const auto expectedEnums = fr.u32();
            CHECK(fr.u8() == tag(f.declaredType) && fr.u8() == tag(f.readType));
            const auto access = (f.get ? 1 : 0) | (f.writable() ? 2 : 0);
            CHECK(fr.u8() == unsigned(access) &&
                  fr.u8() == unsigned(f.readType == ScalarType::Null && access == 0));
            const auto nl = fr.u32(), ul = fr.u32();
            CHECK(fr.raw(nl) == f.name && fr.raw(ul) == f.unit);
            limits(fr, f.declaredType);
            fr.done();
            unsigned ordinal = 0;
            enums(f.declaredType,
                  [&](const Scalar& s, std::string_view name)
                  {
                      auto er = r.record(4);
                      ++count;
                      ++ec;
                      CHECK(er.u32() == e.id() && er.u32() == ordinal++);
                      scalar(er, s);
                      CHECK(er.string() == name);
                      er.done();
                  });
            CHECK(ordinal == expectedEnums);
        }
    }
    CHECK(count == records && fc == fieldsCount && ec == enumsCount);
    r.done();
}

void verifyCommands(const Bytes& bytes, const CommandCatalogIndex& index, std::uint64_t fingerprint)
{
    Reader r{bytes};
    CHECK(r.raw(4) == "TCMD" && r.u16() == 2 && r.u16() == 0);
    CHECK(r.u32() == 44 && r.u32() == bytes.size() && r.number(8) == fingerprint);
    const auto records = r.u32(), catalogs = r.u32(), commandsCount = r.u32(), params = r.u32(),
               enumsCount = r.u32();
    CHECK(catalogs == index.size());
    unsigned count = 0, cc = 0, pc = 0, ec = 0;
    for (auto c : index.catalogs())
    {
        auto cat = r.record(1);
        ++count;
        CHECK(cat.u32() == c.index() && cat.u32() == c.commands().size() &&
              cat.string() == c.name());
        cat.done();
        for (auto e : c.commands())
        {
            auto cr = r.record(2);
            ++count;
            ++cc;
            CHECK(cr.u32() == c.index() && cr.u32() == e.index() && cr.u32() == e.id());
            const auto paramCount = cr.u32();
            CHECK(cr.u32() == 0 && cr.string() == e.command().name);
            cr.done();
            unsigned ordinal = 0;
            if (e.command().hasDescription())
            {
                CHECK(e.command().forEachParameter(
                    [&](const CommandParam& p) noexcept
                    {
                        auto pr = r.record(3);
                        ++count;
                        ++pc;
                        CHECK(pr.u32() == e.id() && pr.u32() == ordinal++ && pr.u32() == 0);
                        const auto enumCount = pr.u32();
                        CHECK(pr.u8() == tag(p.type));
                        CHECK(pr.u8() == unsigned((p.name ? 1 : 0) | (p.unit ? 2 : 0)) &&
                              pr.u16() == 0);
                        const auto nl = pr.u32(), ul = pr.u32();
                        CHECK(pr.raw(nl) == (p.name ? p.name : "") &&
                              pr.raw(ul) == (p.unit ? p.unit : ""));
                        limits(pr, p.type);
                        pr.done();
                        unsigned ord = 0;
                        enums(p.type,
                              [&](const Scalar& s, std::string_view name)
                              {
                                  auto er = r.record(4);
                                  ++count;
                                  ++ec;
                                  CHECK(er.u32() == e.id() && er.u32() == p.index &&
                                        er.u32() == ord++);
                                  scalar(er, s);
                                  CHECK(er.string() == name);
                                  er.done();
                              });
                        CHECK(ord == enumCount);
                        return true;
                    }));
            }
            CHECK(ordinal == paramCount);
        }
    }
    CHECK(count == records && cc == commandsCount && pc == params && ec == enumsCount);
    r.done();
}

template <class File>
Bytes boundaries(const File& file)
{
    const auto all = collect(file, file.size());
    for (auto capacity : {1u, 2u, 3u, 7u, 31u, 63u, 127u, 220u, 256u, 1024u})
    {
        CHECK(collect(file, capacity, true) == all);
    }
    CHECK(file.read(0, {}).status == resource::Status::BufferTooSmall);
    std::array<std::byte, 128> out{};
    for (auto cursor : {UINT64_MAX, UINT64_C(0xffffffff)})
    {
        const auto result = file.read(cursor, out);
        CHECK(result.status == resource::Status::InvalidCursor && result.written == 0 &&
              result.next == cursor);
    }
    // Resume at every byte of every record, including the valid end offset.
    Reader reader{all};
    reader.offset = 44;
    std::vector<std::size_t> starts{0, 44};
    while (reader.offset < all.size())
    {
        reader.number(4);
        auto size = reader.u32();
        reader.raw(size);
        starts.push_back(reader.offset);
    }
    for (std::size_t n = 0; n + 1 < starts.size(); ++n)
    {
        for (std::size_t offset = 0; offset <= starts[n + 1] - starts[n] + 1; ++offset)
        {
            Bytes bytes(all.size());
            const auto cursor = (resource::Cursor(n) << 32) | offset;
            const auto result = file.read(cursor, bytes);
            if (offset > starts[n + 1] - starts[n])
            {
                CHECK(result.status == resource::Status::InvalidCursor && result.written == 0);
            }
            else
            {
                CHECK(result.status == resource::Status::Ok && result.eof);
                CHECK(std::equal(bytes.begin(), bytes.begin() + result.written,
                                 all.begin() + starts[n] + offset, all.end()));
            }
        }
    }
    return all;
}

CommandResult goldenConfigure(float) noexcept
{
    ++invoked;
    return CommandResult::Executed;
}

constexpr FieldTable goldenFields{
    field<&readVoltage>("V", "V", telemetry::limits(230.f, 0.f, 300.f))};
constexpr FieldCatalogTable goldenCatalogs{group("m", goldenFields)};
constexpr CommandTable goldenCommands{
    command<&goldenConfigure>("C", arg<0>("P", "V", 230.f, 0.f, 300.f))};
constexpr CommandCatalogTable goldenCommandCatalogs{group("m", goldenCommands)};

int main(int argc, char** argv)
{
    using namespace telemetry_resource;
    SchemaFile schema{fields.index()};
    CommandsFile commandFile{commands.index()};
    CHECK(getters == 0 && invoked == 0);
    auto sb = boundaries(schema), cb = boundaries(commandFile);
    verifySchema(sb, fields.index(), schema.fingerprint());
    verifyCommands(cb, commands.index(), commandFile.fingerprint());
    CHECK(getters == 0 && invoked == 0);
    SchemaFile gs{goldenCatalogs.index()};
    CommandsFile gc{goldenCommandCatalogs.index()};
    ValuesFile gv{gs};
    CHECK(collect(gs, 7) == unhex(schemaGolden));
    CHECK(collect(gc, 7) == unhex(commandsGolden));
    CHECK(collect(gv, 9) == unhex(valuesGolden));
    const auto gettersBefore = getters;
    const CatalogIndex allIndex{scalarCatalogs};
    SchemaFile allSchema{allIndex};
    ValuesFile vf{allSchema}, independent{allIndex};
    CHECK(getters == gettersBefore && independent.size() == vf.size());
    verifySchema(boundaries(allSchema), allIndex, allSchema.fingerprint());
    CHECK(getters == gettersBefore);
    auto vb = collect(vf, 9);
    CHECK(getters == gettersBefore + 12);
    CHECK(collect(independent, 64) == vb);
    Reader vr{vb};
    CHECK(vr.raw(4) == "TVAL" && vr.u16() == 2 && vr.u16() == 0 &&
          vr.number(8) == allSchema.fingerprint() && vr.u32() == 13);
    for (unsigned i = 0; i < 13; ++i)
    {
        const auto t = tag(scalarRows[i].readType), w = width(t);
        CHECK(vr.u8() == (i >= 11 ? 1u : 0u));
        auto value = vr.number(w);
        const auto mask = w == 8 ? UINT64_MAX : (UINT64_C(1) << (w * 8)) - 1;
        CHECK(value == (i >= 11 ? 0 : bits(values[i]) & mask));
    }
    vr.done();
    // The widened values header may resume at every byte, including within
    // either fingerprint word. Header-only requests must never sample values.
    for (std::size_t offset = 0; offset < 20; ++offset)
    {
        for (std::size_t capacity = 1; capacity <= 20 - offset; ++capacity)
        {
            std::array<std::byte, 22> output{};
            output.fill(std::byte{0xa5});
            const auto before = getters;
            const auto r = vf.read(offset, {output.data() + 1, capacity});
            CHECK(r.status == resource::Status::Ok && r.written == capacity && !r.eof);
            CHECK(r.next == (offset + capacity == 20 ? UINT64_C(1) << 32 : offset + capacity));
            CHECK(
                std::equal(output.begin() + 1, output.begin() + 1 + capacity, vb.begin() + offset));
            CHECK(output.front() == std::byte{0xa5} && output[capacity + 1] == std::byte{0xa5});
            CHECK(getters == before);
        }
    }
    {
        std::array<std::byte, 5> output{};
        const auto before = getters;
        auto r = vf.read(21, output);
        CHECK(r.status == resource::Status::InvalidCursor && r.next == 21 && r.written == 0 &&
              getters == before);
        r = vf.read(20, output);
        CHECK(r.status == resource::Status::Ok && r.written == 5 && getters == before + 1);
        CHECK(std::equal(output.begin(), output.end(), vb.begin() + 20));
    }
    // Each value is atomic. Wrong offsets and undersized output never read it.
    for (unsigned i = 0; i < 13; ++i)
    {
        const auto w = 1 + width(tag(scalarRows[i].readType));
        for (unsigned capacity = 0; capacity < w; ++capacity)
        {
            std::array<std::byte, 9> out{};
            auto before = getters;
            auto cursor = resource::Cursor(i + 1) << 32;
            auto r = vf.read(cursor, {out.data(), capacity});
            CHECK(r.status == resource::Status::BufferTooSmall && r.next == cursor &&
                  r.written == 0 && getters == before);
            r = vf.read(cursor | 1, out);
            CHECK(r.status == resource::Status::InvalidCursor && getters == before);
        }
    }
    std::array<std::byte, 5> first{};
    auto before = getters;
    CHECK(vf.read(UINT64_C(1) << 32, first).written == 5 && getters == before + 1);
    values[0] = Scalar::fromF32(2);
    std::array<std::byte, 5> second{};
    CHECK(vf.read(UINT64_C(1) << 32, second).written == 5 && first != second &&
          getters == before + 2);
    values[0] = Scalar::null();
    CHECK(vf.size() == vb.size());
    CHECK(vf.read(UINT64_C(1) << 32, second).written == 5 && second[0] == std::byte{1});
    CHECK(std::all_of(second.begin() + 1, second.end(),
                      [](auto b)
                      {
                          return b == std::byte{0};
                      }));
    values[0] = Scalar::fromF32(1);
    CHECK(vf.read(UINT64_MAX, second).status == resource::Status::InvalidCursor);
    // Metadata differences alter schema fingerprints; live values do not.
    const auto originalHash = schema.fingerprint();
    voltage = 5;
    CHECK(SchemaFile{fields.index()}.fingerprint() == originalHash);
    voltage = 1;
    const Field varied[] = {{"V", "V", ScalarType::F32, &readVoltage},
                            {"V2", "V", ScalarType::F32, &readVoltage},
                            {"V", "A", ScalarType::F32, &readVoltage},
                            {"V", "V", ScalarType::F64, &readVoltage},
                            {"V", "V", ScalarType::F32, &readVoltage,
                             +[](const Scalar&) noexcept
                             {
                                 return WriteResult::Applied;
                             }},
                            {"V", "V", numericType<float>(1, -1, 1), &readVoltage}};
    std::vector<std::uint64_t> hashes;
    for (const auto& row : varied)
    {
        const Catalog c[]{Catalog{"m", &row, 1}};
        SchemaFile f{CatalogIndex{c}};
        CHECK(f.size() > 0 &&
              std::find(hashes.begin(), hashes.end(), f.fingerprint()) == hashes.end());
        hashes.push_back(f.fingerprint());
    }
    const auto setter = +[](const Scalar&) noexcept
    {
        return WriteResult::Applied;
    };
    const Field policies[]{
        Field{"x", "", ScalarType::F32, &readVoltage, setter},
        Field{"x", "", ScalarType::F32, &readVoltage, setter, FieldFlag::Persistent}};
    const Catalog pa[]{Catalog{"m", &policies[0], 1}}, pb[]{Catalog{"m", &policies[1], 1}};
    CHECK(SchemaFile{CatalogIndex{pa}}.fingerprint() != SchemaFile{CatalogIndex{pb}}.fingerprint());
    static constexpr CommandTable absentLabels{command<&goldenConfigure>("C")};
    static constexpr CommandTable emptyLabels{command<&goldenConfigure>("C", arg<0>("", "", 0.0f))};
    const CommandCatalog ac[]{CommandCatalog{"m", absentLabels.data(), absentLabels.size()}};
    const CommandCatalog bc[]{CommandCatalog{"m", emptyLabels.data(), emptyLabels.size()}};
    CommandsFile absentFile{CommandCatalogIndex{ac}}, emptyFile{CommandCatalogIndex{bc}};
    CHECK(absentFile.fingerprint() != emptyFile.fingerprint());
    verifyCommands(collect(emptyFile, 3), CommandCatalogIndex{bc}, emptyFile.fingerprint());
    const Field invalid[]{Field{nullptr}};
    const Catalog bad[]{Catalog{"m", invalid}};
    SchemaFile broken{CatalogIndex{bad}};
    CHECK(broken.size() == 0 && broken.read(0, first).status == resource::Status::InvalidData);
    ValuesFile badValues{broken};
    CHECK(badValues.size() == 0);
    SchemaFile empty{CatalogIndex{}};
    CommandsFile emptyCommands{CommandCatalogIndex{}};
    ValuesFile emptyValues{empty};
    verifySchema(boundaries(empty), CatalogIndex{}, empty.fingerprint());
    verifyCommands(boundaries(emptyCommands), CommandCatalogIndex{}, emptyCommands.fingerprint());
    CHECK(collect(emptyValues, 1).size() == 20);
    // Transport reconstructs exact bytes using deliberately small READ packets.
    auto fs = resource::filesystem(resource::file("/schema.bin", schema));
    Bytes transported;
    resource::Cursor cursor = 0;
    do
    {
        std::array<std::byte, 13> request{};
        request[0] = std::byte{3};
        for (unsigned i = 0; i < 8; ++i)
        {
            request[5 + i] = std::byte((cursor >> (8 * i)) & 255);
        }
        std::array<std::byte, 19> response{};
        auto result = resource_protocol::process(fs.view(), request, response);
        CHECK(result.status == resource::Status::Ok);
        Reader packet{{response.data(), result.written}};
        CHECK(packet.u8() == 0);
        cursor = packet.number(8);
        const auto eof = packet.u8();
        const auto size = packet.u16();
        CHECK(size == result.written - 12);
        transported.insert(transported.end(), response.begin() + 12,
                           response.begin() + result.written);
        if (eof)
        {
            break;
        }
    } while (transported.size() <= sb.size());
    CHECK(transported == sb && invoked == 0);

    // Exercise both maximum 16-bit positional components without a 2^32-field allocation.
    std::vector<Field> manyFields;
    manyFields.reserve(65536);
    for (unsigned i = 0; i < 65535; ++i)
    {
        manyFields.emplace_back();
    }
    manyFields.emplace_back(
        "last", "", ScalarType::U8,
        +[]() noexcept
        {
            return std::uint8_t{42};
        });
    std::vector<Catalog> manyCatalogs;
    manyCatalogs.reserve(65536);
    for (unsigned i = 0; i < 65535; ++i)
    {
        manyCatalogs.emplace_back("empty", nullptr, 0);
    }
    manyCatalogs.emplace_back("last", manyFields.data(), manyFields.size());
    const CatalogIndex bigIndex{manyCatalogs.data(), manyCatalogs.size()};
    SchemaFile bigSchema{bigIndex};
    ValuesFile bigValues{bigSchema};
    CHECK(bigIndex.find(UINT32_MAX) == &manyFields.back() && bigValues.size() == 20 + 65535 + 2);
    std::array<std::byte, 2> last{};
    auto lastResult = bigValues.read(UINT64_C(65536) << 32, last);
    CHECK(lastResult.eof && lastResult.written == 2 && last[0] == std::byte{0} &&
          last[1] == std::byte{42});
    // Header + flag definition + all catalogs + earlier fields precede the last field.
    std::array<std::byte, 100> record{};
    auto lastField = bigSchema.read(UINT64_C(131073) << 32, record);
    CHECK(lastField.status == resource::Status::Ok && lastField.eof);
    Reader lastReader{{record.data(), lastField.written}};
    auto lastPayload = lastReader.record(3);
    CHECK(lastPayload.u32() == 65535 && lastPayload.u32() == 65535 &&
          lastPayload.u32() == UINT32_MAX);
    lastReader.done();
    if (argc > 1)
    {
        std::filesystem::create_directories(argv[1]);
        save(argv[1], "schema.bin", sb);
        save(argv[1], "commands.bin", cb);
        save(argv[1], "values.bin", collect(ValuesFile{schema}, 9));
        save(argv[1], "all-schema.bin", collect(allSchema, 63));
        save(argv[1], "all-values.bin", vb);
        save(argv[1], "golden-schema.bin", collect(gs, 7));
        save(argv[1], "golden-commands.bin", collect(gc, 7));
        save(argv[1], "golden-values.bin", collect(gv, 9));
    }
    std::printf("Binary telemetry: %u checks\n", checks);
}
