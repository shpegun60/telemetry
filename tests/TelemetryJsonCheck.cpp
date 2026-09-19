// JSON boundary, locale and floating-point round-trip regression checks.
#include "serialization/TelemetryJson.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>

namespace {
using namespace telemetry;
int checks = 0;
int failures = 0;

void expect(bool condition, const char* message)
{
    ++checks;
    if (!condition) ++failures;
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
}

struct Source {
    Scalar value = 123.0f;
    int reads = 0;
    Scalar read() noexcept { ++reads; return value; }
};

using Serialize = std::size_t (*)(const CatalogIndex&, char*, std::size_t) noexcept;

std::size_t writeStringSchema(const CatalogIndex& index, char* buffer, std::size_t size) noexcept
{
    return writeSchema(index, buffer, size, JsonOptions{JsonInt64Mode::String});
}

std::size_t writeStringValues(const CatalogIndex& index, char* buffer, std::size_t size) noexcept
{
    return writeValues(index, buffer, size, JsonOptions{JsonInt64Mode::String});
}

bool buffersAgree(Serialize serialize, const CatalogIndex& index)
{
    char reference[2048];
    const auto length = serialize(index, reference, sizeof(reference));
    bool correct = length > 0 && length + 2 < sizeof(reference);
    for (std::size_t size = 0; size <= length + 2; ++size) {
        char guarded[2056];
        std::fill(std::begin(guarded), std::end(guarded), '#');
        char* const buffer = guarded + 3;
        const auto written = serialize(index, buffer, size);
        const bool fits = size > length;
        correct = correct && written == (fits ? length : 0);
        correct = correct && guarded[0] == '#' && guarded[1] == '#' && guarded[2] == '#';
        for (std::size_t i = size + 3; i < sizeof(guarded); ++i) {
            correct = correct && guarded[i] == '#';
        }
        if (size != 0) correct = correct && std::memchr(buffer, '\0', size) != nullptr;
        if (fits) correct = correct && std::strcmp(buffer, reference) == 0;
    }
    return correct;
}

void checkBuffers(Serialize serialize, const char* message)
{
    Source source;
    const Field fields[] = {
        {0, "first", "V", ScalarType::F32, Getter::bind<&Source::read>(source)},
        {1, "second", "A", ScalarType::F64, []() noexcept { return -0.25; }},
        {2, "maximum", "", ScalarType::U64, []() noexcept { return UINT64_MAX; }},
        {3, "minimum", "", ScalarType::S64, []() noexcept { return INT64_MIN; }},
        {4, "limited", "", numericType<float>(1.2f, -1.25f, 2.5f)},
    };
    const Catalog catalogs[] = {{0, "v", fields}};
    const CatalogIndex index{catalogs};
    expect(buffersAgree(serialize, index), message);
    source.reads = 0;
    expect(serialize(index, nullptr, 0) == 0
               && serialize(index, nullptr, 128) == 0 && source.reads == 0,
           "null output at either size fails without invoking getters");
}

void checkMetadataStrings()
{
    const char label[] = "A\"B\\C\n\t\r\x01\x1f \xc2\xb0" "C";
    const char escaped[] = "\"A\\\"B\\\\C\\u000a\\u0009\\u000d\\u0001\\u001f \xc2\xb0" "C\"";
    char controls[32]{};
    for (unsigned i = 1; i < 32; ++i) controls[i - 1] = static_cast<char>(i);
    const char escapedControls[] =
        "\"\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008"
        "\\u0009\\u000a\\u000b\\u000c\\u000d\\u000e\\u000f\\u0010"
        "\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018"
        "\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f\"";
    Source source;
    const Field fields[] = {
        {0, label, controls, ScalarType::F32, Getter::bind<&Source::read>(source)},
        {1, "", "", ScalarType::Null},
    };
    const Catalog catalogs[] = {{0, label, fields}, {1, controls, nullptr, 0}};
    const CatalogIndex index{catalogs};
    char schema[2048];
    const auto schemaSize = writeSchema(index, schema, sizeof(schema));
    expect(schemaSize != 0
        && std::strstr(schema, (std::string("\"name\":") + escaped + ",\"fields\":[").c_str()) != nullptr
        && std::strstr(schema, (std::string("\"n\":") + escaped + ",\"u\":" + escapedControls).c_str()) != nullptr
        && std::strstr(schema, "\"n\":\"\",\"u\":\"\"") != nullptr
        && std::strstr(schema, (std::string("\"name\":") + escapedControls + ",\"fields\":[]").c_str()) != nullptr,
        "schema escapes quotes, backslashes and all control bytes while preserving UTF-8 and empty strings");
    char values[512];
    expect(writeValues(index, values, sizeof(values)) != 0
        && values == std::string("{") + escaped + ":[123,null]," + escapedControls + ":[]}",
        "value object keys use the same escaped catalog names as schema");
    expect(buffersAgree(static_cast<Serialize>(&writeSchema), index),
        "escaped schema strings respect every output boundary");
    expect(buffersAgree(static_cast<Serialize>(&writeValues), index),
        "escaped value keys respect every output boundary");
    source.reads = 0;
    expect(writeValues(index, values, 6) == 0 && source.reads == 0,
        "truncation inside an escaped catalog name prevents source reads");
}

void checkNullMetadata()
{
    Source source;
    const Field validFields[] = {
        {0, "value", "V", ScalarType::F32, Getter::bind<&Source::read>(source)},
    };
    const Catalog nullCatalogName[] = {{0, nullptr, validFields}};
    char text[512];
    expect(schemaCrc(nullCatalogName, std::size(nullCatalogName)) == 0
               && writeSchema(nullCatalogName, std::size(nullCatalogName), text, sizeof(text)) == 0,
           "schema fingerprint and output reject a null catalog name safely");
    source.reads = 0;
    expect(writeValues(nullCatalogName, std::size(nullCatalogName), text, sizeof(text)) == 0
               && source.reads == 0,
           "value output rejects a null catalog name before invoking getters");

    const Field nullFieldName[] = {
        {0, nullptr, "V", ScalarType::F32, Getter::bind<&Source::read>(source)},
    };
    const Catalog badNameCatalog[] = {{0, "v", nullFieldName}};
    expect(schemaCrc(badNameCatalog, std::size(badNameCatalog)) == 0
               && writeSchema(badNameCatalog, std::size(badNameCatalog), text, sizeof(text)) == 0,
           "schema fingerprint and output reject a null field name safely");

    const Field nullFieldUnit[] = {
        {0, "value", nullptr, ScalarType::F32, Getter::bind<&Source::read>(source)},
    };
    const Catalog badUnitCatalog[] = {{0, "v", nullFieldUnit}};
    expect(schemaCrc(badUnitCatalog, std::size(badUnitCatalog)) == 0
               && writeSchema(badUnitCatalog, std::size(badUnitCatalog), text, sizeof(text)) == 0,
           "schema fingerprint and output reject a null field unit safely");

    source.reads = 0;
    expect(writeValues(badNameCatalog, std::size(badNameCatalog), text, sizeof(text)) != 0
               && writeValues(badUnitCatalog, std::size(badUnitCatalog), text, sizeof(text)) != 0
               && source.reads == 2 && std::strcmp(text, "{\"v\":[123]}") == 0,
           "value output does not inspect schema-only field names or units");
}

void checkInt64Modes()
{
    const Field fields[] = {
        {0, "u64", "", numericType<std::uint64_t>(UINT64_C(5), UINT64_C(1), UINT64_MAX - 1),
            []() noexcept { return UINT64_MAX; }},
        {1, "s64", "", numericType<std::int64_t>(INT64_C(-5), INT64_MIN + 1, INT64_MAX - 1),
            []() noexcept { return INT64_MIN; }},
        {2, "u32", "", ScalarType::U32, []() noexcept { return UINT32_MAX; }},
        {3, "s32", "", ScalarType::S32, []() noexcept { return INT32_MIN; }},
        {4, "flag", "", ScalarType::Bool, []() noexcept { return true; }},
        {5, "nativeU64", "", ScalarType::U64, []() noexcept { return UINT64_C(0); }},
    };
    const Catalog catalogs[] = {{0, "v", fields}};
    const CatalogIndex index{catalogs};
    char numberValues[256];
    char stringValues[256];
    expect(writeValues(index, numberValues, sizeof(numberValues)) != 0
               && std::strcmp(numberValues,
                   "{\"v\":[18446744073709551615,-9223372036854775808,4294967295,-2147483648,true,0]}") == 0,
           "default JSON mode retains numeric U64/S64 output exactly");
    expect(writeValues(catalogs, std::size(catalogs), stringValues, sizeof(stringValues),
                       JsonOptions{JsonInt64Mode::String}) != 0
               && std::strcmp(stringValues,
                   "{\"v\":[\"18446744073709551615\",\"-9223372036854775808\",4294967295,-2147483648,true,\"0\"]}") == 0,
           "string JSON mode quotes only U64/S64 and preserves every digit");

    char numberSchema[1024];
    char stringSchema[1024];
    const auto numberLength = writeSchema(index, numberSchema, sizeof(numberSchema));
    const auto stringLength = writeSchema(index, stringSchema, sizeof(stringSchema),
                                          JsonOptions{JsonInt64Mode::String});
    expect(numberLength != 0 && stringLength != 0
               && std::strstr(numberSchema,
                   "\"t\":\"u64\",\"w\":false,\"min\":1,\"max\":18446744073709551614,\"default\":5") != nullptr
               && std::strstr(stringSchema,
                   "\"t\":\"u64\",\"w\":false,\"min\":\"1\",\"max\":\"18446744073709551614\",\"default\":\"5\"") != nullptr
               && std::strstr(stringSchema,
                   "\"t\":\"s64\",\"w\":false,\"min\":\"-9223372036854775807\",\"max\":\"9223372036854775806\",\"default\":\"-5\"") != nullptr
               && std::strstr(stringSchema,
                   "\"n\":\"nativeU64\",\"u\":\"\",\"t\":\"u64\",\"w\":false,\"min\":null,\"max\":null,\"default\":\"0\"") != nullptr,
           "schema string mode quotes custom U64/S64 bounds and defaults");
    expect(std::strstr(numberSchema, "\"schema\":") != nullptr
               && std::strncmp(numberSchema, stringSchema, 20) == 0
               && schemaCrc(index) != 0,
           "wire representation mode does not change the logical schema fingerprint");
    expect(buffersAgree(&writeStringSchema, index) && buffersAgree(&writeStringValues, index),
           "string-mode schema and values respect every output boundary");
}

void checkEarlyStop()
{
    Source source;
    const Field fields[] = {
        {0, "first", "", ScalarType::F32, Getter::bind<&Source::read>(source)},
        {1, "second", "", ScalarType::F32, Getter::bind<&Source::read>(source)},
    };
    const Catalog catalogs[] = {{0, "v", fields}};
    const CatalogIndex index{catalogs};
    char text[64];
    expect(writeValues(index, text, 2) == 0 && source.reads == 0,
           "a header that does not fit prevents all getter calls");
    source.reads = 0;
    expect(writeValues(index, text, 10) == 0 && source.reads == 1,
           "a full buffer after the first value prevents the next getter call");
    source.reads = 0;
    expect(writeValues(index, text, sizeof(text)) != 0 && source.reads == 2
               && std::strcmp(text, "{\"v\":[123,123]}") == 0,
           "a successful frame calls each getter exactly once");
}

std::uint64_t nextBits(std::uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

template <class T>
void checkRoundTrip(const char* message)
{
    Source source;
    const Field fields[] = {{0, "value", "", Scalar::from(T{}).type(), Getter::bind<&Source::read>(source)}};
    const Catalog catalogs[] = {{0, "v", fields}};
    const CatalogIndex index{catalogs};
    const auto check = [&](T value) {
        source.value = value;
        char text[128];
        const auto written = writeValues(index, text, sizeof(text));
        if (written == 0 || std::strncmp(text, "{\"v\":[", 6) != 0) return false;
        if (!std::isfinite(value)) return std::strcmp(text + 6, "null]}") == 0;
        char* end = nullptr;
        T parsed;
        if constexpr (std::is_same_v<T, float>) parsed = std::strtof(text + 6, &end);
        else parsed = std::strtod(text + 6, &end);
        return end != text + 6 && std::strcmp(end, "]}") == 0 && parsed == value
            && (value != 0 || std::signbit(parsed) == std::signbit(value));
    };
    const T endpoints[] = {T{0}, -T{0}, T{1}, std::nextafter(T{1}, T{2}),
        std::numeric_limits<T>::min(), std::numeric_limits<T>::denorm_min(),
        std::numeric_limits<T>::max(), std::numeric_limits<T>::lowest(),
        std::numeric_limits<T>::infinity(), -std::numeric_limits<T>::infinity(),
        std::numeric_limits<T>::quiet_NaN()};
    bool correct = true;
    for (T value : endpoints) correct = check(value) && correct;
    std::uint64_t state = 0x382fed9421a936ebULL;
    for (int i = 0; i < 4096; ++i) {
        const auto bits = nextBits(state);
        T value;
        std::memcpy(&value, &bits, sizeof(value));
        correct = check(value) && correct;
    }
    expect(correct, message);
}

void checkLocale()
{
    const std::string original = std::setlocale(LC_NUMERIC, nullptr);
    const char* const required = std::getenv("TELEMETRY_TEST_LOCALE");
    const char* candidates[] = {"de_DE.UTF-8", "de_DE.utf8", "German_Germany.1252", "de-DE"};
    bool selected = false;
    if (required != nullptr) selected = std::setlocale(LC_NUMERIC, required) != nullptr;
    else for (const char* candidate : candidates) {
        if (std::setlocale(LC_NUMERIC, candidate)) { selected = true; break; }
    }
    if (!selected) {
        if (required != nullptr) expect(false, "requested numeric test locale is installed");
        else std::printf("SKIP  decimal-comma locale unavailable; set TELEMETRY_TEST_LOCALE to require one\n");
        (void) std::setlocale(LC_NUMERIC, original.c_str());
        return;
    }
    const std::string selectedName = std::setlocale(LC_NUMERIC, nullptr);
    const Field fields[] = {
        {0, "float", "", numericType<float>(1.25f, 0.5f, 2.5f), []() noexcept { return 1.5f; }},
        {1, "double", "", numericType<double>(-2.25, -3.5, 0), []() noexcept { return -2.25; }},
    };
    const Catalog catalogs[] = {{0, "v", fields}};
    char text[128];
    const auto size = writeValues(catalogs, std::size(catalogs), text, sizeof(text));
    expect(std::strcmp(std::localeconv()->decimal_point, ",") == 0,
           "locale regression actually runs with a decimal comma");
    expect(size != 0 && std::strcmp(text, "{\"v\":[1.5,-2.25]}") == 0,
           "JSON keeps a decimal point and correct array length in a comma locale");
    char schema[512];
    expect(writeSchema(catalogs, std::size(catalogs), schema, sizeof(schema)) != 0
        && std::strstr(schema, "\"min\":0.5,\"max\":2.5,\"default\":1.25") != nullptr
        && std::strstr(schema, "\"min\":-3.5,\"max\":0,\"default\":-2.25") != nullptr,
        "custom floating bounds and defaults retain a JSON decimal point in a comma locale");
    expect(selectedName == std::setlocale(LC_NUMERIC, nullptr),
           "serialization does not change the application's numeric locale");
    checkBuffers(static_cast<Serialize>(&writeValues), "locale conversion respects every output boundary");
    expect(std::setlocale(LC_NUMERIC, original.c_str()) != nullptr, "restore numeric locale after the test");
}

template <class T>
void checkIntegerText(const char* message)
{
    Source source;
    const Field fields[] = {{0, "value", "", Scalar::from(T{}).type(), Getter::bind<&Source::read>(source)}};
    const Catalog catalogs[] = {{0, "v", fields}};
    const auto check = [&](T value) {
        source.value = value;
        char text[64];
        if (writeValues(catalogs, std::size(catalogs), text, sizeof(text)) == 0) return false;
        char* end = nullptr;
        bool equal;
        if constexpr (std::is_signed_v<T>) equal = std::strtoll(text + 6, &end, 10) == value;
        else equal = std::strtoull(text + 6, &end, 10) == value;
        return equal && end != text + 6 && std::strcmp(end, "]}") == 0;
    };
    bool correct = check(0) && check(std::numeric_limits<T>::lowest()) && check(std::numeric_limits<T>::max());
    std::uint64_t state = 0x9132c864725d0eafULL;
    for (int i = 0; i < 4096; ++i) {
        const auto bits = nextBits(state);
        if constexpr (std::is_signed_v<T>) {
            const auto magnitude = static_cast<T>(bits & static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
            correct = check((bits >> 63) ? -magnitude : magnitude) && correct;
        } else correct = check(bits) && correct;
    }
    expect(correct, message);
}
} // namespace

int main()
{
    checkBuffers(static_cast<Serialize>(&writeSchema), "schema respects every output boundary");
    checkBuffers(static_cast<Serialize>(&writeValues), "values respect every output boundary");
    checkEarlyStop();
    checkMetadataStrings();
    checkNullMetadata();
    checkInt64Modes();
    checkRoundTrip<float>("F32 boundaries and 4096 bit patterns preserve values and signed zero");
    checkRoundTrip<double>("F64 boundaries and 4096 bit patterns preserve values and signed zero");
    checkIntegerText<std::uint64_t>("U64 boundaries and 4096 values preserve every decimal digit");
    checkIntegerText<std::int64_t>("S64 boundaries and 4096 values preserve every decimal digit");
    checkLocale();
    std::printf("%d/%d telemetry JSON checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
