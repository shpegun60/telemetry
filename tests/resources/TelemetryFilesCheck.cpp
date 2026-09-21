// Differential schemas, cursor boundaries and live-value contracts (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <telemetry/Telemetry.h>
#include <telemetry/serialization/TelemetryCommandJson.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include <resource/FileSystem.hpp>
#include <array>
#include <bit>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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
    field<&readVoltage, &setVoltage>("U\"a", "V\n", limits(1.0f, -10.0f, 300.0f))
        .withFlags(FieldFlag::Persistent),
    field<&readMode>("Mode", ""),
    field<&readLarge>("Large", "", enumSpec<Large::Min, Large::Max>())};
constexpr FieldCatalogTable fields{group("me\"ter", fieldTable)};
constexpr CommandTable commandTable{command<&configure>(
    "Con\nfigure", arg<0>("Voltage", "V", 1.0f, -10.0f, 300.0f), arg<1>("Mode", "", Mode::Auto))};
constexpr CommandCatalogTable commands{group("meter", commandTable)};

template <class File>
std::string collect(const File& file, std::size_t capacity, bool retry = false)
{
    std::vector<std::byte> output(capacity + 2, std::byte{0xa5}), again(capacity);
    resource::Cursor cursor = 0;
    std::string all;
    for (std::size_t calls = 0; calls <= file.size() + 10; ++calls)
    {
        const auto result = file.read(cursor, {output.data() + 1, capacity});
        CHECK(result.status == resource::Status::Ok && result.written <= capacity);
        CHECK(output.front() == std::byte{0xa5} && output.back() == std::byte{0xa5});
        if (retry)
        {
            const auto repeated = file.read(cursor, again);
            CHECK(repeated.status == result.status && repeated.next == result.next &&
                  repeated.eof == result.eof && repeated.written == result.written &&
                  std::memcmp(again.data(), output.data() + 1, result.written) == 0);
        }
        all.append(reinterpret_cast<const char*>(output.data() + 1), result.written);
        if (result.eof)
        {
            CHECK(all.size() == file.size());
            const auto eof = file.read(result.next, {});
            CHECK(eof.status == resource::Status::Ok && eof.written == 0 && eof.eof);
            return all;
        }
        CHECK(result.written != 0 && result.next != cursor);
        cursor = result.next;
    }
    CHECK(false);
    return {};
}

template <class Index, class File>
void differential(const Index& index, const File& file)
{
    std::vector<char> expected(file.size() + 1);
    CHECK(writeSchema(index, expected.data(), expected.size()) == file.size());
    for (std::size_t capacity : {1u, 2u, 3u, 7u, 31u, 63u, 127u, 220u, 256u, 1024u})
    {
        CHECK(collect(file, capacity, true) == expected.data());
    }
    CHECK(file.read(0, {}).status == resource::Status::BufferTooSmall);
    std::array<std::byte, 128> out{};
    const auto invalid = std::numeric_limits<resource::Cursor>::max();
    CHECK(file.read(invalid, out).status == resource::Status::InvalidCursor);
    CHECK(file.read(0xffffffffu, out).status == resource::Status::InvalidCursor);
}

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

int main()
{
    using namespace telemetry_resource;
    SchemaFile schema{fields.index()};
    CommandsFile commandFile{commands.index()};
    CHECK(getters == 0 && invoked == 0);
    differential(fields.index(), schema);
    differential(commands.index(), commandFile);
    CHECK(getters == 0 && invoked == 0);
    // Metadata views copied from temporary indexes remain valid after return.
    const auto copied = schema;
    CHECK(collect(copied, 31) == collect(schema, 127));
    SchemaFile emptyFields{CatalogIndex{}};
    CommandsFile emptyCommands{CommandCatalogIndex{}};
    differential(CatalogIndex{}, emptyFields);
    differential(CommandCatalogIndex{}, emptyCommands);
    const Catalog nullCatalogs[] = {{nullptr, scalarRows}};
    const SchemaFile bad{CatalogIndex{nullCatalogs}};
    CHECK(bad.size() == 0 && bad.read(0, {}).status == resource::Status::InvalidData);
    const Catalog emptyCatalogs[] = {{"empty", nullptr, 0}, {"all", scalarRows}};
    const SchemaFile reserved{CatalogIndex{emptyCatalogs}};
    differential(CatalogIndex{emptyCatalogs}, reserved);

    ValuesFile live{CatalogIndex{scalarCatalogs}};
    const auto size = live.size();
    CHECK(getters == 0 && size > 0);
    const std::string expected =
        "{\"all\":[\"003f800000\",\"008000000000000000\",\"00ff\",\"001234\",\"0012345678\","
        "\"00ffffffffffffffff\",\"00ff\",\"008000\",\"0080000000\",\"008000000000000000\",\"0001\","
        "\"0100000000\",\"01\"],\"empty\":[]}";
    for (std::size_t capacity : {21u, 31u, 63u, 127u, 220u, 256u, 1024u})
    {
        const auto before = getters;
        CHECK(collect(live, capacity) == expected);
        CHECK(getters == before + 12);
    }
    std::array<std::byte, 256> out{};
    const auto prefix = live.read(0, resource::Output{out}.first(8));
    CHECK(prefix.status == resource::Status::Ok && prefix.written == 8);
    const auto before = getters;
    const auto small = live.read(prefix.next, resource::Output{out}.first(11));
    CHECK(small.status == resource::Status::BufferTooSmall && small.next == prefix.next &&
          small.written == 0 && getters == before);
    const auto value = live.read(prefix.next, resource::Output{out}.first(12));
    CHECK(value.status == resource::Status::Ok && value.written == 12 && getters == before + 1);
    CHECK(std::string(reinterpret_cast<char*>(out.data()), 12) == "\"003f800000\"");
    values[0] = Scalar::fromF32(2.0f);
    (void)live.read(prefix.next, resource::Output{out}.first(12));
    CHECK(std::string(reinterpret_cast<char*>(out.data()), 12) == "\"0040000000\"" &&
          getters == before + 2);
    CHECK(live.size() == size);
    CHECK(live.read(prefix.next + 1, out).status == resource::Status::InvalidCursor &&
          getters == before + 2);
    ValuesFile emptyLive{CatalogIndex{}};
    CHECK(collect(emptyLive, 1) == "{}");
    // Long escaped names cross many chunks without retaining temporary text.
    std::string label(2048, 'x');
    label[3] = '\n';
    label[20] = '"';
    const Field longRows[] = {{label.c_str(), "\t", numericType<double>(0.125, -1.25, 10.5)}};
    const Catalog longCatalogs[] = {{label.c_str(), longRows}};
    const SchemaFile longSchema{CatalogIndex{longCatalogs}};
    differential(CatalogIndex{longCatalogs}, longSchema);
    if (const char* locale = std::getenv("TELEMETRY_TEST_LOCALE"))
    {
        CHECK(std::setlocale(LC_NUMERIC, locale) != nullptr);
        differential(CatalogIndex{longCatalogs}, SchemaFile{CatalogIndex{longCatalogs}});
        CHECK(std::setlocale(LC_NUMERIC, "C") != nullptr);
    }
    // Check the new formatter against the existing schema at finite IEEE
    // extremes and varied exponents, not only convenient decimal examples.
    std::uint64_t sample = 91;
    for (unsigned trial = 0; trial < 128; ++trial)
    {
        sample = sample * 6364136223846793005ull + 1;
        const double number = std::bit_cast<double>(sample);
        if (!std::isfinite(number))
        {
            continue;
        }
        const Field row[]{Field{"Number", "", numericType<double>(number)}};
        const Catalog catalog[]{Catalog{"number", row}};
        const CatalogIndex index{catalog};
        const SchemaFile file{index};
        std::vector<char> expectedSchema(file.size() + 1);
        CHECK(writeSchema(index, expectedSchema.data(), expectedSchema.size()) == file.size());
        CHECK(collect(file, 31) == expectedSchema.data());
    }

    std::printf("Telemetry resources: %d checks passed\n", checks);
}
