#include <cstdio>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "TelemetryJson.h"
#include "TelemetryIndex.h"

namespace {

using telemetry::Catalog;
using telemetry::Field;
using telemetry::CatalogIndex;
using telemetry::makeId;
using telemetry::Getter;
using telemetry::Scalar;
using telemetry::ScalarType;
using telemetry::Setter;
using telemetry::WriteResult;

int failures = 0;
int checks = 0;

void expect(bool condition, const char* message)
{
    ++checks;
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}

constexpr Scalar defaultScalar;
constexpr Field defaultField;
constexpr Catalog defaultCatalog;
static_assert(defaultScalar.type() == ScalarType::Null);
static_assert(defaultField.id == 0 && defaultField.name[0] == '\0'
              && defaultField.unit[0] == '\0' && defaultField.declaredType == ScalarType::Null
              && !defaultField.get && !defaultField.set);
static_assert(defaultCatalog.id == 0 && defaultCatalog.name[0] == '\0'
              && defaultCatalog.fields == nullptr && defaultCatalog.count == 0);
static_assert(!std::is_aggregate_v<Field> && std::is_trivially_copyable_v<Field>);
static_assert(alignof(Field) == telemetry::cacheLineBytes);
static_assert(!std::is_copy_assignable_v<Field> && !std::is_move_assignable_v<Field>);
static_assert(std::is_trivially_copyable_v<Scalar> && std::is_standard_layout_v<Scalar>);
static_assert(std::is_nothrow_default_constructible_v<Scalar>
              && std::is_nothrow_default_constructible_v<Field>
              && std::is_nothrow_default_constructible_v<Catalog>);
static_assert(static_cast<unsigned>(ScalarType::Null) == 0
              && static_cast<unsigned>(ScalarType::F32) == 1
              && static_cast<unsigned>(ScalarType::F64) == 2
              && static_cast<unsigned>(ScalarType::U32) == 3
              && static_cast<unsigned>(ScalarType::S32) == 4
              && static_cast<unsigned>(ScalarType::U64) == 5
              && static_cast<unsigned>(ScalarType::Bool) == 6,
              "existing scalar tags retain their numeric codes");

void checkDefaultInitialization()
{
    Scalar value;
    Field field;
    Catalog catalog;
    expect(value.type() == ScalarType::Null && field.id == 0
               && field.name[0] == '\0' && field.unit[0] == '\0'
               && field.declaredType == ScalarType::Null && !field.get && !field.set,
           "plain local declarations initialize Scalar and every Field member");
    expect(field.get().type() == ScalarType::Null,
           "a default Field getter safely returns Null");
    expect(field.write(Scalar::fromF32(1)) == WriteResult::ReadOnly
               && field.set(Scalar::null()) == WriteResult::ReadOnly,
           "a default Field denies writes through both the field and its empty setter");
    expect(catalog.id == 0 && catalog.name[0] == '\0'
               && catalog.fields == nullptr && catalog.count == 0,
           "a plain local Catalog declaration is an empty group");
    const Field partial{makeId(3, 7)};
    expect(partial.id == makeId(3, 7) && partial.name[0] == '\0'
               && partial.unit[0] == '\0' && partial.declaredType == ScalarType::Null
               && !partial.get && !partial.set,
           "omitted row arguments use the same Field defaults");
    const Catalog catalogs[1];
    const CatalogIndex index{catalogs};
    expect(index.size() == 1 && index.find(0) == nullptr,
           "default catalog arrays remain safe empty groups for lookup");
    const Field rows[1];
    const Catalog withDefaultField{0, "defaults", rows};
    char text[64];
    expect(telemetry::writeValues(&withDefaultField, 1, text, sizeof(text)) != 0
               && std::strcmp(text, "{\"defaults\":[null]}") == 0,
           "default field arrays serialize unavailable values");
}

struct ScalarSource {
    Scalar value;
    Scalar read() const noexcept { return value; }
    WriteResult write(const Scalar& next) noexcept { value = next; return WriteResult::Applied; }
};

template <ScalarType Type, auto Make, class Integer>
void checkIntegerScalar(const char* name, const char* minimumText, const char* maximumText)
{
    constexpr Integer minimum = std::numeric_limits<Integer>::lowest();
    constexpr Integer maximum = std::numeric_limits<Integer>::max();
    constexpr Scalar smallest = Make(minimum);
    constexpr Scalar largest = Make(maximum);
    static_assert(smallest.type() == Type && smallest.get<Integer>() == minimum);
    static_assert(largest.type() == Type && largest.get<Integer>() == maximum);
    static_assert(noexcept(Make(minimum)));

    ScalarSource source;
    const Field rows[] = {
        {makeId(0, 0), "value", "", Type, Getter::bind<&ScalarSource::read>(source),
         Setter::bind<&ScalarSource::write>(source)},
    };
    const Catalog catalog{0, "integers", rows};
    const Integer inputs[] = {minimum, 0, maximum};
    const char* expectedNumbers[] = {minimumText, "0", maximumText};
    for (std::size_t i = 0; i < std::size(inputs); ++i) {
        // Exercise copy assignment across variant alternatives, then a bound
        // getter's copy return. Expected decimal text never passes through float.
        source.value = Scalar::fromBool(true);
        const auto result = rows[0].write(Make(inputs[i]));
        const Scalar read = rows[0].get();
        char actual[96], expected[96], message[128];
        std::snprintf(expected, sizeof(expected), "{\"integers\":[%s]}", expectedNumbers[i]);
        std::snprintf(message, sizeof(message), "%s preserves %s through binding and JSON", name, expectedNumbers[i]);
        expect(result == WriteResult::Applied && read.type() == Type && read.get<Integer>() == inputs[i]
                   && telemetry::writeValues(&catalog, 1, actual, sizeof(actual)) != 0
                   && std::strcmp(actual, expected) == 0, message);
    }

    char schema[256], typeText[32], message[96];
    std::snprintf(typeText, sizeof(typeText), "\"t\":\"%s\"", name);
    std::snprintf(message, sizeof(message), "schema identifies %s explicitly", name);
    expect(telemetry::writeSchema(&catalog, 1, schema, sizeof(schema)) != 0
               && std::strstr(schema, typeText) != nullptr, message);
}

void checkIntegerScalars()
{
    checkIntegerScalar<ScalarType::U8, &Scalar::fromU8, std::uint8_t>("u8", "0", "255");
    checkIntegerScalar<ScalarType::U16, &Scalar::fromU16, std::uint16_t>("u16", "0", "65535");
    checkIntegerScalar<ScalarType::U32, &Scalar::fromU32, std::uint32_t>("u32", "0", "4294967295");
    checkIntegerScalar<ScalarType::U64, &Scalar::fromU64, std::uint64_t>("u64", "0", "18446744073709551615");
    checkIntegerScalar<ScalarType::S8, &Scalar::fromS8, std::int8_t>("s8", "-128", "127");
    checkIntegerScalar<ScalarType::S16, &Scalar::fromS16, std::int16_t>("s16", "-32768", "32767");
    checkIntegerScalar<ScalarType::S32, &Scalar::fromS32, std::int32_t>("s32", "-2147483648", "2147483647");
    checkIntegerScalar<ScalarType::S64, &Scalar::fromS64, std::int64_t>("s64", "-9223372036854775808", "9223372036854775807");

    constexpr Field mismatches[] = {
        {makeId(0, 0), "width", "", ScalarType::U16, []() noexcept { return Scalar::fromU8(255); }},
        {makeId(0, 1), "sign", "", ScalarType::U8, []() noexcept { return Scalar::fromS8(-1); }},
        {makeId(0, 2), "signed64", "", ScalarType::S64, []() noexcept { return Scalar::fromU64(UINT64_MAX); }},
        {makeId(0, 3), "unsigned64", "", ScalarType::U64, []() noexcept { return Scalar::fromS64(INT64_MIN); }},
    };
    const Catalog catalog{0, "mismatches", mismatches};
    char text[96];
    expect(telemetry::writeValues(&catalog, 1, text, sizeof(text)) != 0
               && std::strcmp(text, "{\"mismatches\":[255,null,null,null]}") == 0,
           "integer getters normalize to declared types and reject values outside their range");
}

Scalar namedRead() noexcept { return Scalar::fromF32(1.5f); }
float liveLambdaValue = 3.25f;

constexpr Field lambdaFields[] = {
    {makeId(0, 0), "bare", "V", ScalarType::F32, []() noexcept { return Scalar::fromF32(liveLambdaValue); }},
    {makeId(0, 1), "plus", "V", ScalarType::F32, +[]() noexcept { return Scalar::fromF32(liveLambdaValue); }},
};
constexpr Getter implicitLambda = []() noexcept { return Scalar::fromF32(liveLambdaValue); };
constexpr Getter nullGetter{nullptr};
constexpr Getter typedNullGetter{static_cast<Getter::Function>(nullptr)};
static_assert(!nullGetter && !typedNullGetter);
static_assert(noexcept(std::declval<const Getter&>()()), "telemetry reads must remain noexcept");
static_assert(std::is_trivially_copyable_v<Getter>,
              "getters must remain trivial values after native return adaptation");

constexpr Field fields[] = {
    {makeId(0, 0), "f32", "V", ScalarType::F32, &namedRead},
    {makeId(0, 1), "f64", "", ScalarType::F64, +[]() noexcept { return Scalar::fromF64(2.5); }},
    {makeId(0, 2), "u32", "", ScalarType::U32, +[]() noexcept { return Scalar::fromU32(UINT32_MAX); }},
    {makeId(0, 3), "s32", "", ScalarType::S32, +[]() noexcept { return Scalar::fromS32(-5); }},
    {makeId(0, 4), "u64", "", ScalarType::U64, +[]() noexcept { return Scalar::fromU64(UINT64_MAX); }},
    {makeId(0, 5), "flag", "", ScalarType::Bool, +[]() noexcept { return Scalar::fromBool(true); }},
    {makeId(0, 6), "missing", "", ScalarType::U32, +[]() noexcept { return Scalar::null(); }},
    {makeId(0, 7), "mismatch", "", ScalarType::F32, +[]() noexcept { return Scalar::fromU32(7); }},
    {makeId(0, 8), "nan", "", ScalarType::F32, +[]() noexcept { return Scalar::fromF32(std::numeric_limits<float>::quiet_NaN()); }},
    {makeId(0, 9), "inf", "", ScalarType::F64, +[]() noexcept { return Scalar::fromF64(std::numeric_limits<double>::infinity()); }},
    {makeId(0, 10), "empty", "", ScalarType::F32, {}},
};
static_assert(telemetry::names_unique(fields, std::size(fields)));

struct Sensor {
    float value;
    Scalar read() const noexcept { return Scalar::fromF32(value); }
    Scalar increment() noexcept { return Scalar::fromF32(++value); }
};

Sensor globalSensor{10.0f};
constexpr Getter staticBinding = Getter::bind<&Sensor::read>(globalSensor);
static_assert(static_cast<bool>(staticBinding));
static_assert(!static_cast<bool>(Getter{}));
static_assert(std::is_trivially_copyable_v<Getter>);
static_assert(!std::is_constructible_v<Getter, Scalar (*)()>);

template <class T, class = void>
struct CanBind : std::false_type {};
template <class T>
struct CanBind<T, std::void_t<decltype(Getter::bind<&Sensor::read>(std::declval<T>()))>>
    : std::true_type {};
static_assert(CanBind<Sensor&>::value);
static_assert(!CanBind<Sensor&&>::value);

#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(Getter) == 12, "Cortex-M getter size with native return alternatives");
static_assert(sizeof(Setter) == 8, "Cortex-M setter size");
static_assert(sizeof(Scalar) == 16, "Cortex-M scalar size");
static_assert(sizeof(Field) == 96, "Cortex-M field stride must preserve the 32-byte prefix alignment");
static_assert(offsetof(Field, get) == 0 && offsetof(Field, readType) == 12
              && offsetof(Field, set) == 32 && offsetof(Field, declaredType) == 40,
              "Cortex-M read and write contracts must occupy separate lines");
static_assert(sizeof(Catalog) == 16, "Cortex-M catalog size");
static_assert(sizeof(CatalogIndex) == 8, "Cortex-M index size");
#endif

static_assert(telemetry::idComponentCapacity == 65536u);
static_assert(makeId(0, 0) == 0);
static_assert(makeId(UINT16_MAX, UINT16_MAX) == UINT32_MAX);
static_assert(telemetry::groupOf(makeId(0x1234, 0xabcd)) == 0x1234);
static_assert(telemetry::indexOf(makeId(0x1234, 0xabcd)) == 0xabcd);
static_assert(noexcept(std::declval<const CatalogIndex&>().find(0)));

constexpr Field denseFields[] = {
    {makeId(0, 0), "first", "", ScalarType::F32, namedRead},
    {makeId(0, 1), "second", "", ScalarType::F32, namedRead},
    {makeId(0, 2), "third", "", ScalarType::F32, namedRead},
};
constexpr Field gapFields[] = {
    {makeId(1, 0), "kept", "", ScalarType::F32, namedRead},
    {makeId(1, 2), "gap", "", ScalarType::F32, namedRead},
    {makeId(1, 2), "later_valid_position", "", ScalarType::F32, namedRead},
};
constexpr Field laterFields[] = {
    {makeId(2, 0), "later", "", ScalarType::F32, namedRead},
};
constexpr Catalog denseCatalogs[] = {
    {0, "dense", denseFields, std::size(denseFields)},
    {1, "clipped", gapFields, std::size(gapFields)},
    {2, "later_group", laterFields, std::size(laterFields)},
};
constexpr CatalogIndex denseIndex{denseCatalogs, std::size(denseCatalogs)};
static_assert(denseCatalogs[0].count == 3 && denseCatalogs[1].count == 1);
static_assert(denseIndex.size() == 3 && denseIndex.data() == denseCatalogs);
static_assert(denseIndex.find(makeId(0, 0)) == &denseFields[0]);
static_assert(denseIndex.find(makeId(0, 2)) == &denseFields[2]);
static_assert(denseIndex.find(makeId(0, 3)) == nullptr);
static_assert(denseIndex.find(makeId(1, 1)) == nullptr);
static_assert(denseIndex.find(makeId(1, 2)) == nullptr);
static_assert(denseIndex.find(makeId(2, 0)) == &laterFields[0]);
static_assert(denseIndex.find(makeId(3, 0)) == nullptr);
static_assert(denseIndex.find(UINT32_MAX) == nullptr);
static_assert(denseIndex.catalog(1) == &denseCatalogs[1]);
static_assert(denseIndex.catalog(3) == nullptr);

constexpr Catalog brokenGroups[] = {denseCatalogs[0], denseCatalogs[2], denseCatalogs[2]};
constexpr CatalogIndex brokenGroupIndex{brokenGroups, std::size(brokenGroups)};
static_assert(brokenGroupIndex.size() == 1);
static_assert(brokenGroupIndex.find(makeId(0, 2)) == &denseFields[2]);
static_assert(brokenGroupIndex.find(makeId(2, 0)) == nullptr);
constexpr CatalogIndex defaultIndex;
constexpr CatalogIndex nullIndex{nullptr, 123};
static_assert(defaultIndex.size() == 0 && defaultIndex.data() == nullptr);
static_assert(nullIndex.size() == 0 && nullIndex.find(0) == nullptr);
constexpr Catalog nullFields{0, "null", nullptr, 123};
static_assert(nullFields.count == 0);
using FieldArray = Field[3];
using CatalogArray = Catalog[3];
static_assert(std::is_constructible_v<Catalog, telemetry::GroupId, const char*, const FieldArray&>);
static_assert(std::is_constructible_v<Catalog, telemetry::GroupId, const char*, FieldArray&, std::size_t>);
static_assert(!std::is_constructible_v<Catalog, telemetry::GroupId, const char*, FieldArray&&>);
static_assert(!std::is_constructible_v<Catalog, telemetry::GroupId, const char*, const FieldArray&&>);
static_assert(!std::is_constructible_v<Catalog, telemetry::GroupId, const char*, FieldArray&&, std::size_t>);
static_assert(!std::is_constructible_v<Catalog, telemetry::GroupId, const char*, const FieldArray&&, std::size_t>);
static_assert(std::is_constructible_v<CatalogIndex, const CatalogArray&>);
static_assert(std::is_constructible_v<CatalogIndex, CatalogArray&, std::size_t>);
static_assert(!std::is_constructible_v<CatalogIndex, CatalogArray&&>);
static_assert(!std::is_constructible_v<CatalogIndex, const CatalogArray&&>);
static_assert(!std::is_constructible_v<CatalogIndex, CatalogArray&&, std::size_t>);
static_assert(!std::is_constructible_v<CatalogIndex, const CatalogArray&&, std::size_t>);
static_assert(std::is_copy_constructible_v<Catalog> && std::is_move_constructible_v<Catalog>);
static_assert(!std::is_copy_assignable_v<Catalog> && !std::is_move_assignable_v<Catalog>);

void checkIdLookup()
{
    bool matchesRows = true;
    for (telemetry::GroupId group = 0; group < 4; ++group) {
        for (telemetry::FieldOffset offset = 0; offset < 4; ++offset) {
            const Field* expected = nullptr;
            if (group == 0 && offset < 3) expected = &denseFields[offset];
            if (group == 1 && offset == 0) expected = &gapFields[0];
            if (group == 2 && offset == 0) expected = &laterFields[0];
            matchesRows = matchesRows && denseIndex.find(makeId(group, offset)) == expected;
        }
    }
    expect(matchesRows, "packed ID lookup selects the group and exact field offset");
    expect(denseCatalogs[1].count == 1 && denseIndex.find(makeId(1, 2)) == nullptr,
           "a field gap truncates at the first mismatch and later rows never revive");
    expect(denseIndex.size() == 3 && denseIndex.find(makeId(2, 0)) == &laterFields[0],
           "a truncated local field table leaves correctly numbered later groups available");
    const auto copied = denseIndex;
    expect(copied.data() == denseCatalogs && copied.find(makeId(0, 1)) == &denseFields[1],
           "copying the index retains the original catalog and field addresses");
    expect(denseIndex.find(makeId(0, 3)) == nullptr && denseIndex.find(makeId(3, 0)) == nullptr,
           "missing field and group IDs never clamp to the last valid entry");
    expect(denseIndex.catalog(1) == &denseCatalogs[1] && denseIndex.catalog(UINT16_MAX) == nullptr,
           "catalog lookup uses the same bounded group prefix");

    const Field duplicate[] = {denseFields[0], denseFields[0], denseFields[2]};
    const Field reordered[] = {denseFields[1], denseFields[0], denseFields[2]};
    const Field wrongGroup[] = {gapFields[0], denseFields[1]};
    const Catalog duplicateCatalog{0, "duplicate", duplicate, std::size(duplicate)};
    const Catalog reorderedCatalog{0, "reordered", reordered, std::size(reordered)};
    const Catalog wrongGroupCatalog{0, "wrong_group", wrongGroup, std::size(wrongGroup)};
    expect(duplicateCatalog.count == 1 && reorderedCatalog.count == 0 && wrongGroupCatalog.count == 0,
           "duplicate, reordered and wrong-group fields retain only their valid prefix");

    const Catalog duplicateGroups[] = {denseCatalogs[0], denseCatalogs[0], denseCatalogs[2]};
    const CatalogIndex duplicateGroupIndex{duplicateGroups, std::size(duplicateGroups)};
    const CatalogIndex firstWrong{denseCatalogs + 1, 2};
    expect(brokenGroupIndex.size() == 1 && duplicateGroupIndex.size() == 1 && firstWrong.size() == 0,
           "a missing, duplicate or reordered group stops the group prefix");
    expect(brokenGroupIndex.find(makeId(2, 0)) == nullptr && duplicateGroupIndex.catalog(2) == nullptr,
           "later correctly numbered groups cannot revive a broken group prefix");

    const CatalogIndex emptyCount{denseCatalogs, 0};
    const Catalog emptyCatalog{0, "zero_count", denseFields, 0};
    expect(defaultIndex.size() == 0 && emptyCount.size() == 0 && nullIndex.size() == 0
               && nullIndex.catalog(0) == nullptr && nullFields.count == 0 && emptyCatalog.count == 0,
           "default, zero-count and null inputs produce empty accessible prefixes");
    const Catalog nullThenValid[] = {nullFields, denseCatalogs[1]};
    const CatalogIndex nullThenValidIndex{nullThenValid, std::size(nullThenValid)};
    expect(nullThenValidIndex.size() == 2 && nullThenValidIndex.find(0) == nullptr
               && nullThenValidIndex.find(makeId(1, 0)) == &gapFields[0],
           "a null local field array remains an empty group without hiding the next group");

    char rawSchema[2048], indexedSchema[2048], rawValues[256], indexedValues[256];
    telemetry::writeSchema(denseCatalogs, std::size(denseCatalogs), rawSchema, sizeof(rawSchema));
    telemetry::writeSchema(denseIndex, indexedSchema, sizeof(indexedSchema));
    telemetry::writeValues(denseCatalogs, std::size(denseCatalogs), rawValues, sizeof(rawValues));
    telemetry::writeValues(denseIndex, indexedValues, sizeof(indexedValues));
    expect(std::strcmp(rawSchema, indexedSchema) == 0 && std::strcmp(rawValues, indexedValues) == 0
               && telemetry::schemaCrc(denseCatalogs, std::size(denseCatalogs)) == telemetry::schemaCrc(denseIndex),
           "raw and indexed JSON overloads use the identical validated prefixes");
    expect(std::strcmp(indexedValues, "{\"dense\":[1.5,1.5,1.5],\"clipped\":[1.5],\"later_group\":[1.5]}") == 0
               && std::strstr(indexedSchema, "later_valid_position") == nullptr,
           "serialization includes only retained local fields and keeps later valid groups");
    expect(std::strstr(indexedSchema, "\"id\":131072,\"n\":\"later\"") != nullptr,
           "schema emits the packed group component in each numeric field ID");
    telemetry::writeValues(brokenGroups, std::size(brokenGroups), rawValues, sizeof(rawValues));
    expect(std::strcmp(rawValues, "{\"dense\":[1.5,1.5,1.5]}") == 0,
           "raw JSON overloads clip broken group order instead of serializing a later match");
    expect(telemetry::schemaCrc(brokenGroupIndex) == telemetry::schemaCrc(denseCatalogs, 1),
           "clipped groups do not contribute to the schema fingerprint");
    telemetry::writeValues(nullIndex, rawValues, sizeof(rawValues));
    expect(std::strcmp(rawValues, "{}") == 0, "empty index serializes an empty value object");
    expect(telemetry::writeValues(nullptr, 7, rawValues, sizeof(rawValues)) == 2
               && std::strcmp(rawValues, "{}") == 0
               && telemetry::schemaCrc(nullptr, 7) == telemetry::schemaCrc(nullIndex),
           "raw null catalog arrays clip a nonzero requested count before serialization");
}

void checkIdCapacity()
{
    // Test fixtures allocate on the host; the library index still owns no storage.
    const std::size_t capacity = telemetry::idComponentCapacity;
    std::vector<Field> maximumFields;
    maximumFields.reserve(capacity + 1);
    for (std::size_t i = 0; i <= capacity; ++i) {
        maximumFields.push_back({makeId(UINT16_MAX, static_cast<telemetry::FieldOffset>(i)),
                                 "limit", "", ScalarType::F32, namedRead});
    }
    const Catalog maximumCatalog{UINT16_MAX, "maximum", maximumFields.data(), maximumFields.size()};
    expect(maximumCatalog.count == capacity && maximumFields[capacity - 1].id == UINT32_MAX,
           "a catalog retains all 65536 offsets including 65535 and clips an extra row");

    std::vector<Catalog> maximumCatalogs;
    maximumCatalogs.reserve(capacity + 1);
    for (std::size_t i = 0; i < capacity - 1; ++i) {
        maximumCatalogs.emplace_back(static_cast<telemetry::GroupId>(i), "empty", nullptr, 0);
    }
    maximumCatalogs.push_back(maximumCatalog);
    maximumCatalogs.emplace_back(0, "beyond_capacity", nullptr, 0);
    const CatalogIndex maximumIndex{maximumCatalogs.data(), maximumCatalogs.size()};
    expect(maximumIndex.size() == capacity && maximumIndex.catalog(UINT16_MAX) == &maximumCatalogs[capacity - 1],
           "the index retains all 65536 groups including 65535 and clips an extra group");
    expect(maximumIndex.find(UINT32_MAX) == &maximumFields[capacity - 1]
               && maximumIndex.find(makeId(UINT16_MAX, 0)) == &maximumFields[0]
               && maximumIndex.find(makeId(0, UINT16_MAX)) == nullptr,
           "full-width IDs address the exact endpoint without wrapping to another group");
    const Field* endpoint = maximumIndex.find(UINT32_MAX);
    expect(endpoint != nullptr && endpoint->get().get<float>() == 1.5f,
           "the maximum packed ID invokes the selected getter");
    std::vector<char> schema(capacity * 256u + 1024u);
    expect(telemetry::writeSchema(maximumIndex, schema.data(), schema.size()) != 0
               && std::strstr(schema.data(), "{\"id\":65535,\"name\":\"maximum\"") != nullptr
               && std::strstr(schema.data(), "\"id\":4294967295,") != nullptr,
           "schema preserves maximum group and packed field IDs without narrowing");
}

void checkBindings()
{
    expect(staticBinding().get<float>() == 10.0f, "constexpr binding to a global object");
    expect(Getter::bind<&namedRead>()().get<float>() == 1.5f, "named function template binding");

    Sensor first{20.0f};
    Sensor second{40.0f};
    const Field runtimeFields[] = {
        {makeId(0, 0), "first", "V", ScalarType::F32, Getter::bind<&Sensor::read>(first)},
        {makeId(0, 1), "second", "V", ScalarType::F32, Getter::bind<&Sensor::read>(second)},
    };
    const Catalog catalog{0, "sensors", runtimeFields, std::size(runtimeFields)};
    first.value = 25.0f;
    char text[128];
    telemetry::writeValues(&catalog, 1, text, sizeof(text));
    expect(std::strcmp(text, "{\"sensors\":[25,40]}") == 0,
           "runtime table reads the bound instances and observes changes");
    const CatalogIndex index{&catalog, 1};
    const auto copiedIndex = index;
    expect(index.size() == 1 && index.find(makeId(0, 0)) == &runtimeFields[0]
               && copiedIndex.find(makeId(0, 1)) == &runtimeFields[1],
           "runtime index and its copy retain the actual bound rows");
    first.value = 27.0f;
    expect(index.find(makeId(0, 0)) != nullptr && copiedIndex.find(makeId(0, 0)) != nullptr
               && index.find(makeId(0, 0))->get().get<float>() == 27.0f
               && copiedIndex.find(makeId(0, 0))->get().get<float>() == 27.0f,
           "ID lookup reads changing source data without rebuilding definitions");
    first.value = 25.0f;

    const Sensor immutable{50.0f};
    expect(Getter::bind<&Sensor::read>(immutable)().get<float>() == 50.0f, "binding to a const object");
    expect(Getter::bind<&Sensor::increment>(second)().get<float>() == 41.0f, "binding a non-const method");

    Getter replaced = runtimeFields[0].get;
    expect(replaced().get<float>() == 25.0f, "copy preserves the bound object");
    replaced = &namedRead;
    expect(replaced().get<float>() == 1.5f, "assignment from bound to plain function");
    replaced = runtimeFields[1].get;
    expect(replaced().get<float>() == 41.0f, "assignment from plain to bound function");
    replaced = Getter{};
    expect(!replaced && replaced().type() == ScalarType::Null, "empty getter returns Null");
}

void checkGetterPolicy()
{
    auto throwingLambda = [] { return Scalar::fromF32(0.0f); };
    auto capturefulLambda = [value = 2.0f]() noexcept { return Scalar::fromF32(value); };
    static_assert(!std::is_constructible_v<Getter, decltype(throwingLambda)>,
                  "potentially throwing lambda must not enter a noexcept getter");
    static_assert(!std::is_constructible_v<Getter, decltype(capturefulLambda)>,
                  "a temporary capture cannot be kept by a non-owning getter");
    static_assert(!std::is_constructible_v<Getter, decltype(capturefulLambda)&>,
                  "a captureful lvalue needs an explicit lifetime-safe binding");
    static_assert(!std::is_assignable_v<Getter&, Scalar (*)()>,
                  "assignment must retain the noexcept function restriction");
    static_assert(!std::is_assignable_v<Getter&, decltype(throwingLambda)>,
                  "assignment must retain the noexcept lambda restriction");

    liveLambdaValue = 6.5f;
    const Catalog catalog{0, "lambda", lambdaFields, std::size(lambdaFields)};
    char text[128];
    telemetry::writeValues(&catalog, 1, text, sizeof(text));
    expect(std::strcmp(text, "{\"lambda\":[6.5,6.5]}") == 0,
           "constexpr bare and plus lambda rows serialize live values");
    expect(implicitLambda().get<float>() == 6.5f,
           "implicit lambda construction retains the live function target");

    auto lambda = []() noexcept { return Scalar::fromF32(liveLambdaValue); };
    Getter assigned = lambda;
    liveLambdaValue = 8.25f;
    expect(assigned().get<float>() == 8.25f, "getter accepts a named noexcept lambda");
    assigned = []() noexcept { return Scalar::fromF32(-liveLambdaValue); };
    expect(assigned().get<float>() == -8.25f, "assignment accepts a bare noexcept lambda");
    assigned = lambda;
    expect(assigned().get<float>() == 8.25f, "assignment accepts a named noexcept lambda");

    expect(nullGetter().type() == ScalarType::Null && typedNullGetter().type() == ScalarType::Null,
           "nullptr and typed null function construction stay safely empty");
    assigned = static_cast<Getter::Function>(nullptr);
    expect(!assigned && assigned().type() == ScalarType::Null,
           "typed null function assignment resets the getter without a delegate assertion");
    assigned = lambda;
    assigned = nullptr;
    expect(!assigned && assigned().type() == ScalarType::Null,
           "nullptr assignment resets a populated getter");
}

void checkJson()
{
    const Catalog catalogs[] = {{0, "t", fields, std::size(fields)}, {1, "empty", nullptr, 0}};
    char schema[2048];
    const auto schemaLength = telemetry::writeSchema(catalogs, std::size(catalogs), schema, sizeof(schema));
    expect(schemaLength != 0, "schema serializes");
    expect(std::strstr(schema, "{\"i\":4,\"id\":4,\"n\":\"u64\",\"u\":\"\",\"t\":\"u64\",\"w\":false,") != nullptr,
           "schema preserves field index, type and name");
    expect(std::strstr(schema, "{\"i\":0,\"id\":0,\"n\":\"f32\"") != nullptr,
           "schema emits zero as a numeric field ID");
    expect(std::strstr(schema, "{\"id\":0,\"name\":\"t\"") != nullptr
               && std::strstr(schema, "{\"id\":1,\"name\":\"empty\"") != nullptr,
           "schema emits the numeric group IDs");

    char values[512];
    const auto valuesLength = telemetry::writeValues(catalogs, std::size(catalogs), values, sizeof(values));
    expect(valuesLength != 0, "values serialize");
    expect(std::strcmp(values,
        "{\"t\":[1.5,2.5,4294967295,-5,18446744073709551615,true,null,7,null,null,null],\"empty\":[]}") == 0,
        "mixed scalar types, full U64 digits, unavailable and non-finite values");

    expect(telemetry::writeValues(catalogs, std::size(catalogs), values, valuesLength) == 0,
           "buffer also needs space for its terminator");
    expect(telemetry::writeValues(catalogs, std::size(catalogs), values, valuesLength + 1) == valuesLength,
           "exactly sized buffer succeeds");
    char tiny[8] = {};
    expect(telemetry::writeSchema(catalogs, std::size(catalogs), tiny, sizeof(tiny)) == 0,
           "short schema buffer reports failure");
    expect(telemetry::writeValues(catalogs, std::size(catalogs), nullptr, 0) == 0,
           "zero-size values buffer reports failure");
    expect(telemetry::writeValues(nullptr, 0, values, sizeof(values)) == 2 && std::strcmp(values, "{}") == 0,
           "empty catalog list serializes");
}

void checkSchemaIdentity()
{
    constexpr Field a[] = {{makeId(0, 0), "bc", "", ScalarType::F32, &namedRead}};
    constexpr Field b[] = {{makeId(0, 0), "c", "", ScalarType::F32, &namedRead}};
    const Catalog ca{0, "a", a, 1};
    const Catalog cb{0, "ab", b, 1};
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(&cb, 1),
           "schema hash separates string boundaries");

    const Field changedType[] = {{a[0].id, a[0].name, a[0].unit, ScalarType::U32, a[0].get, a[0].set}};
    const Catalog typeVariant{0, "a", changedType, 1};
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(&typeVariant, 1),
           "schema hash includes declared types");
    const Field changedUnit[] = {{a[0].id, a[0].name, "V", a[0].declaredType, a[0].get, a[0].set}};
    const Catalog unitVariant{0, "a", changedUnit, 1};
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(&unitVariant, 1),
           "schema hash includes units");
    const Field invalidSuffix[] = {a[0], {makeId(0, 5), "ignored", "", ScalarType::F32, namedRead}};
    const Catalog clipped{0, "a", invalidSuffix, std::size(invalidSuffix)};
    expect(clipped.count == 1 && telemetry::schemaCrc(&ca, 1) == telemetry::schemaCrc(&clipped, 1),
           "clipped field suffixes do not contribute to the schema fingerprint");

    const Catalog separate[] = {
        {0, "a", nullptr, 0}, {1, "bc", nullptr, 0}, {2, "", nullptr, 0}, {3, "f32", nullptr, 0},
    };
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(separate, std::size(separate)),
           "schema hash separates fields from catalogs");

    constexpr Field duplicate[] = {a[0], a[0]};
    expect(!telemetry::names_unique(duplicate, std::size(duplicate)), "duplicate names are detected");
}

void checkFieldNames()
{
    constexpr Field unique[] = {{0, "meter"}, {1, "sensor"}, {2, ""}};
    constexpr Field nullFirst[] = {{0, nullptr}, {1, "sensor"}};
    constexpr Field nullLater[] = {{0, "meter"}, {1, nullptr}};
    constexpr Field emptyNames[] = {{0, ""}, {1, ""}};
    static_assert(telemetry::names_unique(unique, std::size(unique)));
    static_assert(telemetry::names_unique(nullptr, 0));
    static_assert(!telemetry::names_unique(nullptr, 1));
    static_assert(telemetry::names_unique(nullFirst, 0));
    static_assert(!telemetry::names_unique(nullFirst, 1));
    static_assert(!telemetry::names_unique(nullFirst, std::size(nullFirst)));
    static_assert(!telemetry::names_unique(nullLater, std::size(nullLater)));
    static_assert(!telemetry::names_unique(emptyNames, std::size(emptyNames)));

    // Runtime-created definitions also exercise the checks under sanitizers.
    const Field rows[] = {unique[0], unique[1], unique[2]};
    expect(telemetry::names_unique(rows, 0) && telemetry::names_unique(rows, 1)
        && telemetry::names_unique(rows, std::size(rows)),
        "empty, single and distinct field names are accepted");
    expect(telemetry::names_unique(nullptr, 0) && !telemetry::names_unique(nullptr, 1),
        "null field storage is valid only with an empty count");
    const Field firstNullRows[] = {nullFirst[0], rows[1], rows[2]};
    expect(telemetry::names_unique(firstNullRows, 0) && !telemetry::names_unique(firstNullRows, 1)
        && !telemetry::names_unique(firstNullRows, std::size(firstNullRows)),
        "a null first field name is rejected even for one field");
    const Field laterNullRows[] = {rows[0], rows[1], {2, nullptr}};
    expect(!telemetry::names_unique(laterNullRows, std::size(laterNullRows)), "a later null field name is rejected");
    const char separateName[] = {'m', 'e', 't', 'e', 'r', '\0'};
    const Field duplicateRows[] = {rows[0], rows[1], {2, separateName}};
    expect(!telemetry::names_unique(duplicateRows, std::size(duplicateRows)),
        "field name uniqueness compares text from different storage");
    expect(!telemetry::names_unique(emptyNames, std::size(emptyNames)),
        "duplicate empty field names are rejected");
}

void checkCatalogNames()
{
    constexpr Catalog unique[] = {{0, "meter", nullptr, 0}, {1, "sensor", nullptr, 0}};
    static_assert(telemetry::catalog_names_unique(unique, std::size(unique)));
    static_assert(telemetry::catalog_names_unique(nullptr, 0));
    expect(telemetry::catalog_names_unique(unique, 0) && telemetry::catalog_names_unique(unique, 1)
        && telemetry::catalog_names_unique(unique, std::size(unique)),
        "empty, single and distinct catalog names are accepted");

    const char separateName[] = {'m', 'e', 't', 'e', 'r', '\0'};
    const Catalog duplicate[] = {unique[0], {1, separateName, nullptr, 0}};
    expect(!telemetry::catalog_names_unique(duplicate, std::size(duplicate)),
        "catalog name uniqueness compares text from different storage");
    constexpr Catalog emptyNames[] = {{0, "", nullptr, 0}, {1, "", nullptr, 0}};
    static_assert(!telemetry::catalog_names_unique(emptyNames, std::size(emptyNames)));
    expect(!telemetry::catalog_names_unique(emptyNames, std::size(emptyNames)),
        "duplicate empty catalog names are rejected");
    constexpr Catalog nullName{0, nullptr, nullptr, 0};
    static_assert(!telemetry::catalog_names_unique(&nullName, 1));
    expect(!telemetry::catalog_names_unique(nullptr, 1)
        && !telemetry::catalog_names_unique(&nullName, 1),
        "nonempty null catalog storage and null names fail validation");
}

} // namespace

int main()
{
    checkDefaultInitialization();
    checkIntegerScalars();
    checkBindings();
    checkGetterPolicy();
    checkIdLookup();
    checkIdCapacity();
    checkJson();
    checkSchemaIdentity();
    checkFieldNames();
    checkCatalogNames();
    std::printf("%d/%d telemetry checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
