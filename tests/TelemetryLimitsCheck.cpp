// Limits are metadata and a write contract; reads only normalize their type.
#include "TelemetryEnum.h"
#include "TelemetryJson.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {
using namespace telemetry;
int checks = 0, failures = 0;
void expect(bool condition, const char* message)
{
    ++checks;
    if (!condition) ++failures;
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
}
struct Owner {
    Scalar value = std::uint16_t{100};
    int reads = 0, writes = 0;
    Scalar read() noexcept { ++reads; return value; }
    WriteResult write(const Scalar& next) noexcept { ++writes; value = next; return WriteResult::Applied; }
};
Owner source;
enum class Mode : std::int16_t { Low = -4, High = 8 };
enum class Wide : std::uint64_t { Low = UINT64_MAX - 2, High = UINT64_MAX };
enum class Signed : std::int64_t { Low = INT64_MIN, High = INT64_MAX };
constexpr auto bounded = numericType<std::uint16_t>(10, 20, 15);
constexpr Field rows[] = {{0, "level", "", bounded,
    Getter::bind<&Owner::read>(source), Setter::bind<&Owner::write>(source)}};
constexpr Catalog catalogs[] = {{0, "v", rows}};
constexpr auto index = CatalogIndex::bind<catalogs>();
static_assert(bounded.minimum().get<std::uint16_t>() == 10);
static_assert(bounded.maximum().get<std::uint16_t>() == 20);
static_assert(bounded.defaultValue().get<std::uint16_t>() == 15);
static_assert(numericType<std::uint16_t>(10, 20, 12.75).defaultValue().get<std::uint16_t>() == 12);
static_assert(enumType<Mode>().minimum().get<std::int16_t>() == -4);
static_assert(enumType<Mode>().maximum().get<std::int16_t>() == 8);
static_assert(enumType<Mode>().defaultValue().get<std::int16_t>() == -4);
static_assert(enumType<Mode>(Mode::High).defaultValue().get<std::int16_t>() == 8);
static_assert(enumType<Mode, Mode::High, Mode::Low>().defaultValue().get<std::int16_t>() == -4);
static_assert(enumType<Wide, Wide::Low, Wide::High>().minimum().get<std::uint64_t>() == UINT64_MAX - 2);
static_assert(enumType<Signed, Signed::High, Signed::Low>().minimum().get<std::int64_t>() == INT64_MIN);
static_assert(std::is_same_v<decltype(index.read<0>()), std::optional<std::uint16_t>>);

template <class T>
void checkNative()
{
    constexpr FieldType implicit = Scalar::from(T{}).type();
    constexpr auto named = numericType<T>();
    static_assert(implicit.minimum().template get<T>() == std::numeric_limits<T>::lowest());
    static_assert(implicit.maximum().template get<T>() == std::numeric_limits<T>::max());
    static_assert(implicit.defaultValue().template get<T>() == T{});
    static_assert(named.minimum().template get<T>() == std::numeric_limits<T>::lowest());
    Owner owner;
    const Field field{0, "native", "", implicit, Getter::bind<&Owner::read>(owner), Setter::bind<&Owner::write>(owner)};
    expect(field.write(std::numeric_limits<T>::lowest()) == WriteResult::Applied
        && owner.value.get<T>() == std::numeric_limits<T>::lowest(), "native minimum is writable without precision loss");
    expect(field.write(std::numeric_limits<T>::max()) == WriteResult::Applied
        && owner.value.get<T>() == std::numeric_limits<T>::max(), "native maximum is writable without precision loss");
}

void checkRange()
{
    source = Owner{};
    expect(source.value.get<std::uint16_t>() == 100 && source.writes == 0, "default metadata does not initialize the owner");
    const Field& field = rows[0];
    expect(field.write(10) == WriteResult::Applied && source.writes == 1, "inclusive custom lower bound");
    expect(field.write(20) == WriteResult::Applied && source.writes == 2, "inclusive custom upper bound");
    expect(field.write(9) == WriteResult::InvalidValue && field.write(21) == WriteResult::InvalidValue
        && source.writes == 2 && source.value.get<std::uint16_t>() == 20, "out-of-range writes leave owner untouched");
    expect(index.write(0, 20.99) == WriteResult::Applied && source.value.get<std::uint16_t>() == 20,
           "write checks the converted integer after fraction truncation");
    expect(index.write(0, 9.99) == WriteResult::InvalidValue, "truncated value below the lower bound is rejected");
    expect(index.write(0, 65536) == WriteResult::InvalidValue, "numeric conversion fails before the custom bounds");
    expect(source.reads == 0, "write validation never reads the source");
    source.value = 100.75;
    expect(index.read<0>() == 100 && index.read<double>(0) == 100.0,
           "reads normalize to declared U16 and ignore custom min/max");
    char values[64];
    expect(writeValues(index, values, sizeof(values)) != 0 && std::strcmp(values, "{\"v\":[100]}") == 0,
           "value serialization does not clamp or reject out-of-interval reads");
    source.value = 65536u;
    expect(index.read(0).type() == ScalarType::Null, "reads retain declared-type representability checks");
    const Field readonly{0, "readonly", "", bounded};
    expect(readonly.write(65536) == WriteResult::ReadOnly, "setter presence is checked before type and interval validation");
    expect(rows[0].write(bounded.defaultValue()) == WriteResult::Applied
        && source.value.get<std::uint16_t>() == 15, "default is applied only by an explicit write");
}

void checkFloating()
{
    Owner owner;
    const Field full{0, "full", "", ScalarType::F32, Getter::bind<&Owner::read>(owner), Setter::bind<&Owner::write>(owner)};
    for (float value : {INFINITY, -INFINITY, std::numeric_limits<float>::quiet_NaN()}) {
        owner.value = value;
        expect(full.write(value) == WriteResult::InvalidValue && owner.writes == 0,
               "finite native bounds reject NaN and infinity before setter");
        const auto read = full.read<float>();
        expect(read && (std::isnan(value) ? std::isnan(*read) : *read == value),
               "floating reads preserve special values regardless of write bounds");
    }
    const Field limited{0, "limited", "", numericType<float>(-1.0f, 1.0f, -0.0f), nullptr, Setter::bind<&Owner::write>(owner)};
    expect(limited.write(-1.0f) == WriteResult::Applied && limited.write(1.0f) == WriteResult::Applied,
           "float interval endpoints are inclusive");
    expect(limited.write(std::nextafter(1.0f, 2.0f)) == WriteResult::InvalidValue
        && limited.write(std::nextafter(-1.0f, -2.0f)) == WriteResult::InvalidValue,
           "adjacent float values outside limits are rejected");
    expect(limited.write(std::nextafter(1.0, 2.0)) == WriteResult::Applied,
           "interval checks follow the declared F32 rounding of a double input");
    expect(std::signbit(limited.declaredType.defaultValue().get<float>()), "default metadata preserves signed zero");
}

void checkEnumsAndWideValues()
{
    Owner owner;
    const Field mode{0, "mode", "", enumType<Mode>(), nullptr, Setter::bind<&Owner::write>(owner)};
    expect(mode.write(0) == WriteResult::Applied && owner.value.get<std::int16_t>() == 0,
           "a gap between enum codes remains writable; no membership check");
    expect(mode.write(-5) == WriteResult::InvalidValue && mode.write(9) == WriteResult::InvalidValue,
           "enum extrema become the numeric write limits");
    const Field wide{0, "wide", "", enumType<Wide, Wide::Low, Wide::High>(), nullptr, Setter::bind<&Owner::write>(owner)};
    expect(wide.write(UINT64_MAX - 1) == WriteResult::Applied
        && owner.value.get<std::uint64_t>() == UINT64_MAX - 1, "U64 limits and gaps compare without a double intermediate");
    expect(wide.write(UINT64_MAX - 3) == WriteResult::InvalidValue, "U64 lower endpoint remains exact");
    const Field negative{0, "negative", "", numericType<std::int64_t>(INT64_MIN, INT64_MIN + 2, INT64_MIN),
        nullptr, Setter::bind<&Owner::write>(owner)};
    expect(negative.write(INT64_MIN + 1) == WriteResult::Applied
        && negative.write(INT64_MIN + 3) == WriteResult::InvalidValue, "S64 limits compare exactly near minimum");
    const Field yes{0, "yes", "", numericType<bool>(true, true, true), nullptr, Setter::bind<&Owner::write>(owner)};
    expect(yes.write(2) == WriteResult::Applied && yes.write(0) == WriteResult::InvalidValue,
           "bool interval is checked after the ordinary nonzero conversion");
    auto changed = enumType<Mode>().withDefault(0);
    expect(changed.hasEnum() && changed.defaultValue().get<std::int16_t>() == 0,
           "changing numeric default preserves enum descriptions");
    changed = ScalarType::U8;
    expect(!changed.hasEnum() && changed.maximum().get<std::uint8_t>() == 255
        && changed.defaultValue().get<std::uint8_t>() == 0, "numeric type assignment restores native limits and zero default");
}

std::uint32_t fingerprint(FieldType type)
{
    const Field fields[] = {{0, "value", "", type}};
    const Catalog groups[] = {{0, "v", fields}};
    return schemaCrc(groups, 1);
}

void checkSchema()
{
    const Field fields[] = {
        {0, "ordinary", "", ScalarType::U8},
        {1, "restricted", "", bounded},
        {2, "mode", "", enumType<Mode>(Mode::High)},
        {3, "empty", "", ScalarType::Null},
        {4, "wide", "", enumType<Wide, Wide::Low, Wide::High>()},
    };
    const Catalog groups[] = {{0, "v", fields}};
    char text[2048];
    expect(writeSchema(groups, 1, text, sizeof(text)) != 0, "schema with required limits serializes");
    expect(std::strstr(text, "\"min\":0,\"max\":255,\"default\":0") != nullptr, "ordinary read-only fields export native extrema and default");
    expect(std::strstr(text, "\"min\":10,\"max\":20,\"default\":15") != nullptr, "custom interval and default export unconditionally");
    expect(std::strstr(text, "\"min\":-4,\"max\":8,\"default\":8,\"enum\"") != nullptr, "enum schema includes extrema and chosen default");
    expect(std::strstr(text, "\"min\":null,\"max\":null,\"default\":null") != nullptr, "Null metadata still exports all required properties");
    expect(std::strstr(text, "\"min\":18446744073709551613,\"max\":18446744073709551615,\"default\":18446744073709551613") != nullptr,
           "64-bit metadata exports exact integer digits");
    expect(fingerprint(bounded) != fingerprint(numericType<std::uint16_t>(9, 20, 15)), "minimum participates in schema fingerprint");
    expect(fingerprint(bounded) != fingerprint(numericType<std::uint16_t>(10, 21, 15)), "maximum participates in schema fingerprint");
    expect(fingerprint(bounded) != fingerprint(bounded.withDefault(16)), "default participates in schema fingerprint");
    expect(fingerprint(ScalarType::F32) == fingerprint(numericType<float>()), "equivalent type construction produces identical metadata fingerprint");
}

void checkSchemaRoundTrip()
{
    Owner owner;
    const Field fields[] = {{0, "f32", "", numericType<float>().withDefault(1.2f),
        nullptr, Setter::bind<&Owner::write>(owner)}};
    const Catalog groups[] = {{0, "v", fields}};
    char text[1024];
    const bool serialized = writeSchema(groups, 1, text, sizeof(text)) != 0;
    const char* const properties[] = {"\"min\":", "\"max\":", "\"default\":"};
    const float expected[] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), 1.2f};
    for (std::size_t i = 0; i < 3; ++i) {
        const char* const property = serialized ? std::strstr(text, properties[i]) : nullptr;
        char* end = nullptr;
        const double parsed = property ? std::strtod(property + std::strlen(properties[i]), &end) : 0;
        expect(property && end != property + std::strlen(properties[i])
            && parsed == static_cast<double>(expected[i]) && fields[0].write(parsed) == WriteResult::Applied
            && owner.value.get<float>() == expected[i],
            "F32 metadata parsed as double writes back exactly, including native extrema");
    }
}
}

int main()
{
    checkNative<float>(); checkNative<double>(); checkNative<bool>();
    checkNative<std::uint8_t>(); checkNative<std::uint16_t>(); checkNative<std::uint32_t>(); checkNative<std::uint64_t>();
    checkNative<std::int8_t>(); checkNative<std::int16_t>(); checkNative<std::int32_t>(); checkNative<std::int64_t>();
    checkRange(); checkFloating(); checkEnumsAndWideValues(); checkSchema(); checkSchemaRoundTrip();
    std::printf("%d/%d telemetry limit checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
