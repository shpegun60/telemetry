// Enum metadata must describe schemas without restricting numeric data paths.
#include "field/TelemetryEnum.h"
#include "serialization/TelemetryJson.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <type_traits>

namespace examples {
enum class Mode : std::uint16_t { Off, Auto, Manual };
enum class Same : std::uint16_t { Off, Auto, Manual };
enum class Renamed : std::uint16_t { Off, Automatic, Manual };
enum class Recoded : std::uint16_t { Off, Auto, Manual = 3 };
enum class Byte : std::uint8_t { Zero, Last = UINT8_MAX };
enum class Wide : std::uint64_t { First = 100000, Last = UINT64_MAX };
enum class Signed : std::int64_t { First = INT64_MIN, Last = INT64_MAX };
enum class OddName : std::int8_t { Entry = -1 };
enum class NulA : std::uint8_t { Entry };
enum class NulB : std::uint8_t { Entry };
enum class Flag : bool { No, Yes };
template <class T> struct Small { enum class Value : T { Zero, One }; };
}

namespace magic_enum::customize {
template <> struct enum_range<examples::Byte> { static constexpr int min = 0, max = 255; };
template <> constexpr customize_t enum_name(examples::OddName value) noexcept
{
    if (value == examples::OddName::Entry) return std::string_view{"quote\"slash\\\n\t\0end", 18};
    return invalid_tag;
}
template <> constexpr customize_t enum_name(examples::NulA value) noexcept
{
    if (value == examples::NulA::Entry) return std::string_view{"a\0b", 3};
    return invalid_tag;
}
template <> constexpr customize_t enum_name(examples::NulB value) noexcept
{
    if (value == examples::NulB::Entry) return std::string_view{"a\0c", 3};
    return invalid_tag;
}
}

constexpr auto directEnumHeaderSpec = telemetry::enumSpec<
    examples::Mode::Off, examples::Mode::Auto, examples::Mode::Manual>();
static_assert(directEnumHeaderSpec.initial == examples::Mode::Off,
              "TelemetryEnum.h must expose enumSpec directly");

namespace {
using namespace telemetry;
using namespace examples;
int checks = 0, failures = 0;
int reads = 0, writes = 0;
std::uint16_t current = 1;

void expect(bool condition, const char* message)
{
    ++checks;
    if (!condition) ++failures;
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
}

std::uint16_t readMode() noexcept { ++reads; return current; }
WriteResult writeMode(const Scalar& value) noexcept
{
    ++writes;
    current = value.get<std::uint16_t>();
    return WriteResult::Applied;
}

constexpr Field fields[] = {{0, "Mode", "", enumType<Mode>(), readMode, writeMode}};
constexpr Catalog catalogs[] = {{0, "v", fields}};
constexpr auto index = CatalogIndex::bind<catalogs>();
static_assert(fields[0].declaredType == ScalarType::U16);
static_assert(fields[0].declaredType.hasEnum());
static_assert(std::is_same_v<decltype(index.read<0>()), std::optional<std::uint16_t>>);
static_assert(!std::is_aggregate_v<Field> && std::is_trivially_copyable_v<Field>);
static_assert(!std::is_constructible_v<Scalar, Mode>);
static_assert(!detail::isScalarReadType<Mode> && !detail::isScalarNumber<Mode>);
static_assert(Scalar::typeCount == 12);

template <class Raw>
void checkBase()
{
    using E = typename Small<Raw>::Value;
    // Clang instantiates a class-template enum's enumerator list on first
    // named use. The automatic scan requires that definition to be available.
    static_assert(E::Zero == static_cast<E>(0) && E::One == static_cast<E>(1));
    constexpr auto type = enumType<E>();
    static_assert(type == Scalar::from(Raw{}).type());
    struct Context { int calls = 0; bool correct = true; } context;
    const bool done = type.describeEnum(&context, [](void* p, const Scalar& value, std::string_view name) noexcept {
        auto& state = *static_cast<Context*>(p);
        const auto number = convertScalar<Raw>(value);
        state.correct = state.correct && value.type() == Scalar::from(Raw{}).type()
            && number && *number == static_cast<Raw>(state.calls)
            && name == (state.calls == 0 ? "Zero" : "One");
        ++state.calls;
        return true;
    });
    expect(done && context.correct && context.calls == 2, "enum underlying type and entries retain native numeric type");
}

std::uint32_t fingerprint(FieldType type)
{
    const Field rows[] = {{0, "Mode", "", type, readMode, writeMode}};
    const Catalog groups[] = {{0, "v", rows}};
    return schemaCrc(groups, std::size(groups));
}

std::string schema(FieldType type)
{
    const Field rows[] = {{0, "Mode", "", type, readMode, writeMode}};
    const Catalog groups[] = {{0, "v", rows}};
    char text[1024];
    const auto length = writeSchema(groups, std::size(groups), text, sizeof(text));
    return {text, length};
}

void checkDescriptions()
{
    constexpr FieldType empty;
    static_assert(empty == ScalarType::Null && !empty.hasEnum());
    constexpr FieldType numeric = ScalarType::F32;
    static_assert(numeric == ScalarType::F32 && !numeric.hasEnum());
    expect(!empty.describeEnum(nullptr, nullptr), "empty descriptor rejects an absent sink");
    expect(!fields[0].declaredType.describeEnum(nullptr, nullptr), "enum descriptor rejects an absent sink");
    auto copy = fields[0].declaredType;
    expect(copy.hasEnum() && copy == ScalarType::U16, "descriptor copy preserves type and metadata");
    copy = ScalarType::U8;
    expect(copy == ScalarType::U8 && !copy.hasEnum(), "assigning a numeric type clears enum metadata");
    const auto accept = +[](void*, const Scalar&, std::string_view) noexcept { return true; };
    expect(!copy.describeEnum(nullptr, accept), "numeric descriptor does not invoke a sink");
    expect(fields[0].declaredType.describeEnum(nullptr, accept), "a stateless sink can use a null context");
    int calls = 0;
    const bool done = fields[0].declaredType.describeEnum(&calls,
        [](void* p, const Scalar&, std::string_view) noexcept {
            ++*static_cast<int*>(p);
            return false;
        });
    expect(!done && calls == 1, "a refusing sink stops enumeration immediately");
}

void checkNumericPaths()
{
    reads = writes = 0;
    current = 100;
    expect(index.read<0>() == 100 && reads == 1, "inferred read returns underlying number even without a dictionary entry");
    expect(index.read<double>(0) == 100.0 && reads == 2, "typed read converts the numeric value normally");
    const auto scalar = index.read(0);
    expect(scalar.type() == ScalarType::U16 && scalar.get<std::uint16_t>() == 100 && reads == 3,
           "Scalar read keeps U16 without introducing an enum alternative");
    expect(index.write(0, 65535) == WriteResult::InvalidValue && current == 100 && writes == 0 && reads == 3,
           "numeric enum extrema reject an out-of-interval code before the setter");
    expect(index.write(0, 2.75f) == WriteResult::Applied && current == 2 && writes == 1,
           "ordinary float-to-U16 truncation applies to enum metadata too");
    expect(index.write(0, 65536) == WriteResult::InvalidValue && current == 2 && writes == 1,
           "out-of-range write is rejected before invoking the owner");
    expect(index.write(0, -1) == WriteResult::InvalidValue && writes == 1, "negative U16 write is rejected");
    const Field converted{0, "converted", "", enumType<Mode>(), []() noexcept { return 12.75; }};
    expect(converted.read<double>() == 12.0, "read normalization still follows the declared underlying type");
    const Field invalid{0, "invalid", "", enumType<Mode>(), []() noexcept { return 65536u; }};
    expect(invalid.read().type() == ScalarType::Null, "out-of-range getter is unavailable");
    char text[64];
    expect(writeValues(index, text, sizeof(text)) != 0 && std::strcmp(text, "{\"v\":[2]}") == 0,
           "values JSON contains only numbers, never enum names");
}

void checkSchemas()
{
    reads = writes = 0;
    const auto automatic = schema(enumType<Mode>());
    expect(automatic.find("\"t\":\"u16\",\"w\":true,\"min\":0,\"max\":2,\"default\":0,\"enum\":{\"0\":\"Off\",\"1\":\"Auto\",\"2\":\"Manual\"}") != std::string::npos,
           "schema keeps the numeric type and adds the generated dictionary");
    expect(automatic == schema(enumType<Mode, Mode::Off, Mode::Auto, Mode::Manual>()),
           "automatic and explicit identical entries produce identical schema and fingerprint");
    expect(automatic == schema(enumType<Same>()), "schema depends on codes and labels, not the C++ type or function address");
    expect(fingerprint(enumType<Mode>()) != fingerprint(enumType<Renamed>()), "renamed label changes fingerprint");
    expect(fingerprint(enumType<Mode>()) != fingerprint(enumType<Recoded>()), "changed code changes fingerprint");
    expect(fingerprint(enumType<Mode>()) != fingerprint(enumType<Mode, Mode::Manual, Mode::Auto, Mode::Off>()),
           "explicit entry order participates in fingerprint");
    expect(fingerprint(enumType<Mode>()) != fingerprint(ScalarType::U16), "adding a dictionary changes fingerprint");
    const auto plain = schema(ScalarType::U16);
    expect(plain.find("\"enum\"") == std::string::npos, "ordinary numeric fields have no dictionary property");
    expect(schema(enumType<Byte>()).find("\"enum\":{\"0\":\"Zero\",\"255\":\"Last\"}") != std::string::npos,
           "configured scan range includes U8 maximum");
    expect(schema(enumType<Wide, Wide::First, Wide::Last>()).find("\"enum\":{\"100000\":\"First\",\"18446744073709551615\":\"Last\"}") != std::string::npos,
           "explicit sparse U64 entries keep every decimal digit including maximum");
    expect(schema(enumType<Signed, Signed::First, Signed::Last>()).find("\"enum\":{\"-9223372036854775808\":\"First\",\"9223372036854775807\":\"Last\"}") != std::string::npos,
           "explicit S64 endpoints use exact signed keys without signed overflow");
    expect(schema(enumType<Flag>()).find("\"t\":\"bool\",\"w\":true,\"min\":false,\"max\":true,\"default\":false,\"enum\":{\"0\":\"No\",\"1\":\"Yes\"}") != std::string::npos,
           "bool enum uses bool values and zero/one dictionary keys");
    expect(schema(enumType<OddName>()).find("\"-1\":\"quote\\\"slash\\\\\\u000a\\u0009\\u0000end\"") != std::string::npos,
           "custom names escape quotes, slashes, controls and embedded NUL");
    expect(fingerprint(enumType<NulA>()) != fingerprint(enumType<NulB>()), "fingerprint includes bytes after an embedded NUL");
    expect(reads == 0 && writes == 0, "schema and fingerprint never call source getters or setters");
}

void checkBuffers()
{
    const Field rows[] = {
        {0, "Mode", "", enumType<Mode>(), readMode},
        {1, "Signed", "", enumType<Signed, Signed::First, Signed::Last>()},
        {2, "Escaped", "", enumType<OddName>()},
    };
    const Catalog groups[] = {{0, "v", rows}};
    const CatalogIndex view{groups};
    char reference[1024];
    const auto length = writeSchema(view, reference, sizeof(reference));
    bool correct = length != 0 && length + 2 < sizeof(reference);
    reads = writes = 0;
    for (std::size_t size = 0; size <= length + 2; ++size) {
        char guarded[1032];
        std::fill(std::begin(guarded), std::end(guarded), '#');
        char* const buffer = guarded + 3;
        const auto written = writeSchema(view, buffer, size);
        const bool fits = size > length;
        correct = correct && written == (fits ? length : 0);
        for (std::size_t i = 0; i < 3; ++i) correct = correct && guarded[i] == '#';
        for (std::size_t i = size + 3; i < sizeof(guarded); ++i) correct = correct && guarded[i] == '#';
        if (size != 0) correct = correct && std::memchr(buffer, '\0', size) != nullptr;
        if (fits) correct = correct && std::strcmp(buffer, reference) == 0;
    }
    expect(correct, "enum schema respects every output boundary, including inside escaped names");
    expect(writeSchema(view, nullptr, 0) == 0 && writeSchema(view, nullptr, 512) == 0,
           "enum schema rejects null output at any size");
    expect(reads == 0 && writes == 0, "truncated enum schema never invokes source callbacks");
}
}

int main()
{
    checkBase<std::uint8_t>(); checkBase<std::int8_t>();
    checkBase<std::uint16_t>(); checkBase<std::int16_t>();
    checkBase<std::uint32_t>(); checkBase<std::int32_t>();
    checkBase<std::uint64_t>(); checkBase<std::int64_t>();
    checkBase<bool>(); checkBase<char>();
    checkDescriptions(); checkNumericPaths(); checkSchemas(); checkBuffers();
    std::printf("%d/%d telemetry enum checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
