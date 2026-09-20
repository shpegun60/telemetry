#include <cstdio>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/TelemetryId.h"
#include "serialization/TelemetryJson.h"
#include "catalog/TelemetryIndex.h"

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
static_assert(defaultField.name[0] == '\0'
              && defaultField.unit[0] == '\0' && defaultField.declaredType == ScalarType::Null
              && !defaultField.get && !defaultField.set);
static_assert(defaultCatalog.name[0] == '\0'
              && defaultCatalog.fields == nullptr && defaultCatalog.count == 0);
static_assert(!std::is_aggregate_v<Field> && std::is_trivially_copyable_v<Field>);
static_assert(alignof(Field) == telemetry::cacheLineBytes);
static_assert(!std::is_copy_assignable_v<Field> && !std::is_move_assignable_v<Field>);
// std::variant's standard-layout status differs across standard libraries;
// the public ABI contract requires Scalar to remain trivially copyable.
static_assert(std::is_trivially_copyable_v<Scalar>);
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
    expect(value.type() == ScalarType::Null
               && field.name[0] == '\0' && field.unit[0] == '\0'
               && field.declaredType == ScalarType::Null && !field.get && !field.set,
           "plain local declarations initialize Scalar and every Field member");
    expect(field.get().type() == ScalarType::Null,
           "a default Field getter safely returns Null");
    expect(field.write(Scalar::fromF32(1)) == WriteResult::ReadOnly
               && field.set(Scalar::null()) == WriteResult::ReadOnly,
           "a default Field denies writes through both the field and its empty setter");
    expect(catalog.name[0] == '\0'
               && catalog.fields == nullptr && catalog.count == 0,
           "a plain local Catalog declaration is an empty group");
    const Field partial{"partial"};
    expect(std::strcmp(partial.name, "partial") == 0
               && partial.unit[0] == '\0' && partial.declaredType == ScalarType::Null
               && !partial.get && !partial.set,
           "omitted row arguments use the same Field defaults");
    const Catalog catalogs[1];
    const CatalogIndex index{catalogs};
    expect(index.size() == 1 && index.find(0) == nullptr,
           "default catalog arrays remain safe empty groups for lookup");
    const Field rows[1];
    const Catalog withDefaultField{"defaults", rows};
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
        {"value", "", Type, Getter::bind<&ScalarSource::read>(source),
         Setter::bind<&ScalarSource::write>(source)},
    };
    const Catalog catalog{"integers", rows};
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
        {"width", "", ScalarType::U16, []() noexcept { return Scalar::fromU8(255); }},
        {"sign", "", ScalarType::U8, []() noexcept { return Scalar::fromS8(-1); }},
        {"signed64", "", ScalarType::S64, []() noexcept { return Scalar::fromU64(UINT64_MAX); }},
        {"unsigned64", "", ScalarType::U64, []() noexcept { return Scalar::fromS64(INT64_MIN); }},
    };
    const Catalog catalog{"mismatches", mismatches};
    char text[96];
    expect(telemetry::writeValues(&catalog, 1, text, sizeof(text)) != 0
               && std::strcmp(text, "{\"mismatches\":[255,null,null,null]}") == 0,
           "integer getters normalize to declared types and reject values outside their range");
}

Scalar namedRead() noexcept { return Scalar::fromF32(1.5f); }
float liveLambdaValue = 3.25f;

constexpr Field lambdaFields[] = {
    {"bare", "V", ScalarType::F32, []() noexcept { return Scalar::fromF32(liveLambdaValue); }},
    {"plus", "V", ScalarType::F32, +[]() noexcept { return Scalar::fromF32(liveLambdaValue); }},
};
constexpr Getter implicitLambda = []() noexcept { return Scalar::fromF32(liveLambdaValue); };
constexpr Getter nullGetter{nullptr};
constexpr Getter typedNullGetter{static_cast<Getter::Function>(nullptr)};
static_assert(!nullGetter && !typedNullGetter);
static_assert(noexcept(std::declval<const Getter&>()()), "telemetry reads must remain noexcept");
static_assert(std::is_trivially_copyable_v<Getter>,
              "getters must remain trivial values after native return adaptation");

constexpr Field fields[] = {
    {"f32", "V", ScalarType::F32, &namedRead},
    {"f64", "", ScalarType::F64, +[]() noexcept { return Scalar::fromF64(2.5); }},
    {"u32", "", ScalarType::U32, +[]() noexcept { return Scalar::fromU32(UINT32_MAX); }},
    {"s32", "", ScalarType::S32, +[]() noexcept { return Scalar::fromS32(-5); }},
    {"u64", "", ScalarType::U64, +[]() noexcept { return Scalar::fromU64(UINT64_MAX); }},
    {"flag", "", ScalarType::Bool, +[]() noexcept { return Scalar::fromBool(true); }},
    {"missing", "", ScalarType::U32, +[]() noexcept { return Scalar::null(); }},
    {"mismatch", "", ScalarType::F32, +[]() noexcept { return Scalar::fromU32(7); }},
    {"nan", "", ScalarType::F32, +[]() noexcept { return Scalar::fromF32(std::numeric_limits<float>::quiet_NaN()); }},
    {"inf", "", ScalarType::F64, +[]() noexcept { return Scalar::fromF64(std::numeric_limits<double>::infinity()); }},
    {"empty", "", ScalarType::F32, {}},
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
static_assert(sizeof(Getter) == 8, "Cortex-M getter must remain payload plus invoker");
static_assert(sizeof(Setter) == 8, "Cortex-M setter size");
static_assert(sizeof(Scalar) == 16, "Cortex-M scalar size");
static_assert(sizeof(Field) == 96, "Cortex-M field stride must preserve the 32-byte prefix alignment");
static_assert(offsetof(Field, get) == 0 && offsetof(Field, readType) == 8
              && offsetof(Field, set) == 32 && offsetof(Field, declaredType) == 40,
              "Cortex-M read and write contracts must occupy separate lines");
static_assert(sizeof(Catalog) == 12, "Cortex-M catalog size");
static_assert(sizeof(CatalogIndex) == 8, "Cortex-M index size");
#endif

static_assert(telemetry::idComponentCapacity == 65536u);
static_assert(std::is_same_v<telemetry::FieldId, telemetry::PackedId>
              && std::is_same_v<telemetry::CommandId, telemetry::PackedId>);
static_assert(std::is_same_v<telemetry::FieldOffset, telemetry::EntryOffset>
              && std::is_same_v<telemetry::CommandOffset, telemetry::EntryOffset>);
static_assert(makeId(0, 0) == 0);
static_assert(makeId(UINT16_MAX, UINT16_MAX) == UINT32_MAX);
static_assert(telemetry::groupOf(makeId(0x1234, 0xabcd)) == 0x1234);
static_assert(telemetry::indexOf(makeId(0x1234, 0xabcd)) == 0xabcd);
static_assert(noexcept(std::declval<const CatalogIndex&>().find(0)));

constexpr Field denseFields[] = {
    {"first", "", ScalarType::F32, namedRead},
    {"second", "", ScalarType::F32, namedRead},
    {"third", "", ScalarType::F32, namedRead},
};
constexpr Field gapFields[] = {
    {"kept", "", ScalarType::F32, namedRead},
    {"gap", "", ScalarType::F32, namedRead},
    {"later_valid_position", "", ScalarType::F32, namedRead},
};
constexpr Field laterFields[] = {
    {"later", "", ScalarType::F32, namedRead},
};
constexpr Catalog denseCatalogs[] = {
    {"dense", denseFields, std::size(denseFields)},
    {"clipped", gapFields, std::size(gapFields)},
    {"later_group", laterFields, std::size(laterFields)},
};
constexpr CatalogIndex denseIndex{denseCatalogs, std::size(denseCatalogs)};
static_assert(denseCatalogs[0].count == 3 && denseCatalogs[1].count == 3);
static_assert(denseIndex.size() == 3 && denseIndex.data() == denseCatalogs);
static_assert(denseIndex.find(makeId(0, 0)) == &denseFields[0]);
static_assert(denseIndex.find(makeId(0, 2)) == &denseFields[2]);
static_assert(denseIndex.find(makeId(0, 3)) == nullptr);
static_assert(denseIndex.find(makeId(1, 1)) == &gapFields[1]);
static_assert(denseIndex.find(makeId(1, 2)) == &gapFields[2]);
static_assert(denseIndex.find(makeId(2, 0)) == &laterFields[0]);
static_assert(denseIndex.find(makeId(3, 0)) == nullptr);
static_assert(denseIndex.find(UINT32_MAX) == nullptr);
static_assert(denseIndex.catalog(1) == &denseCatalogs[1]);
static_assert(denseIndex.catalog(3) == nullptr);

constexpr Catalog brokenGroups[] = {denseCatalogs[0], denseCatalogs[2], denseCatalogs[2]};
constexpr CatalogIndex brokenGroupIndex{brokenGroups, std::size(brokenGroups)};
static_assert(brokenGroupIndex.size() == 3);
static_assert(brokenGroupIndex.find(makeId(0, 2)) == &denseFields[2]);
static_assert(brokenGroupIndex.find(makeId(2, 0)) == laterFields);
constexpr CatalogIndex defaultIndex;
constexpr CatalogIndex nullIndex{nullptr, 123};
static_assert(defaultIndex.size() == 0 && defaultIndex.data() == nullptr);
static_assert(nullIndex.size() == 0 && nullIndex.find(0) == nullptr);
constexpr Catalog nullFields{"null", nullptr, 123};
static_assert(nullFields.count == 0);
using FieldArray = Field[3];
using CatalogArray = Catalog[3];
static_assert(std::is_constructible_v<Catalog, const char*, const FieldArray&>);
static_assert(std::is_constructible_v<Catalog, const char*, FieldArray&, std::size_t>);
static_assert(!std::is_constructible_v<Catalog, const char*, FieldArray&&>);
static_assert(!std::is_constructible_v<Catalog, const char*, const FieldArray&&>);
static_assert(!std::is_constructible_v<Catalog, const char*, FieldArray&&, std::size_t>);
static_assert(!std::is_constructible_v<Catalog, const char*, const FieldArray&&, std::size_t>);
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
    bool correct = true;
    for (telemetry::GroupId g = 0; g < 4; ++g) {
        for (telemetry::FieldOffset i = 0; i < 4; ++i) {
            const Field* expected = nullptr;
            if (g < 3 && i < denseCatalogs[g].count) expected = denseCatalogs[g].fields + i;
            correct = correct && denseIndex.find(makeId(g, i)) == expected;
        }
    }
    expect(correct, "position alone determines both packed ID components");
    const Field reordered[] = {denseFields[2], Field{}, denseFields[0]};
    const Catalog groups[] = {{"reordered", reordered}, {"empty", nullptr, 30}, denseCatalogs[0]};
    const CatalogIndex view{groups};
    expect(view.find(0) == &reordered[0] && view.find(1) == &reordered[1]
           && view.find(2) == &reordered[2], "reordering changes identity and empty slots keep their position");
    expect(view.read(1).type() == ScalarType::Null && view.write(1, 3) == WriteResult::ReadOnly,
           "reserved field slots are unavailable and read-only");
    expect(view.find(makeId(1, 0)) == nullptr && view.find(makeId(2, 0)) == denseFields,
           "empty groups preserve later group positions");
    const CatalogIndex copy = view;
    expect(copy.data() == groups && copy.find(2) == &reordered[2], "index copies borrow original descriptors");
    expect(nullIndex.size() == 0 && defaultIndex.size() == 0 && nullFields.count == 0,
           "null arrays remain empty");
    char raw[4096], indexed[4096];
    expect(telemetry::writeSchema(groups, 3, raw, sizeof raw) != 0
           && telemetry::writeSchema(view, indexed, sizeof indexed) != 0
           && std::strcmp(raw, indexed) == 0, "raw and indexed schemas use identical positional IDs");
    expect(std::strstr(raw, "\"id\":131072,\"n\":\"first\"") != nullptr,
           "schema derives packed IDs from traversal positions");
    expect(telemetry::writeValues(view, raw, sizeof raw) != 0
           && std::strcmp(raw, "{\"reordered\":[1.5,null,1.5],\"empty\":[],\"dense\":[1.5,1.5,1.5]}") == 0,
           "value traversal preserves reserved slots and empty groups");
    expect(telemetry::writeValues(nullptr, 7, raw, sizeof raw) == 2
           && std::strcmp(raw, "{}") == 0, "raw null arrays never access elements");
}

void checkIdCapacity()
{
    // Test fixtures allocate on the host; the library index still owns no storage.
    const std::size_t capacity = telemetry::idComponentCapacity;
    std::vector<Field> maximumFields;
    maximumFields.reserve(capacity + 1);
    for (std::size_t i = 0; i <= capacity; ++i) {
        maximumFields.push_back({"limit", "", ScalarType::F32, namedRead});
    }
    const Catalog maximumCatalog{"maximum", maximumFields.data(), maximumFields.size()};
    expect(maximumCatalog.count == capacity,
           "a catalog retains all 65536 offsets including 65535 and clips an extra row");

    std::vector<Catalog> maximumCatalogs;
    maximumCatalogs.reserve(capacity + 1);
    for (std::size_t i = 0; i < capacity - 1; ++i) {
        maximumCatalogs.emplace_back("empty", nullptr, 0);
    }
    maximumCatalogs.push_back(maximumCatalog);
    maximumCatalogs.emplace_back("beyond_capacity", nullptr, 0);
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
        {"first", "V", ScalarType::F32, Getter::bind<&Sensor::read>(first)},
        {"second", "V", ScalarType::F32, Getter::bind<&Sensor::read>(second)},
    };
    const Catalog catalog{"sensors", runtimeFields, std::size(runtimeFields)};
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
    const Catalog catalog{"lambda", lambdaFields, std::size(lambdaFields)};
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
    const Catalog catalogs[] = {{"t", fields, std::size(fields)}, {"empty", nullptr, 0}};
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
    constexpr Field a[] = {{"bc", "", ScalarType::F32, &namedRead}};
    constexpr Field b[] = {{"c", "", ScalarType::F32, &namedRead}};
    const Catalog ca{"a", a, 1};
    const Catalog cb{"ab", b, 1};
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(&cb, 1),
           "schema hash separates string boundaries");

    const Field changedType[] = {{a[0].name, a[0].unit, ScalarType::U32, a[0].get, a[0].set}};
    const Catalog typeVariant{"a", changedType, 1};
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(&typeVariant, 1),
           "schema hash includes declared types");
    const Field changedUnit[] = {{a[0].name, "V", a[0].declaredType, a[0].get, a[0].set}};
    const Catalog unitVariant{"a", changedUnit, 1};
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(&unitVariant, 1),
           "schema hash includes units");
    const Field invalidSuffix[] = {a[0], {"ignored", "", ScalarType::F32, namedRead}};
    const Catalog clipped{"a", invalidSuffix, std::size(invalidSuffix)};
    expect(clipped.count == 2 && telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(&clipped, 1),
           "every positional field contributes to the schema fingerprint");

    const Catalog separate[] = {
        {"a", nullptr, 0}, {"bc", nullptr, 0}, {"", nullptr, 0}, {"f32", nullptr, 0},
    };
    expect(telemetry::schemaCrc(&ca, 1) != telemetry::schemaCrc(separate, std::size(separate)),
           "schema hash separates fields from catalogs");

    constexpr Field duplicate[] = {a[0], a[0]};
    expect(!telemetry::names_unique(duplicate, std::size(duplicate)), "duplicate names are detected");
}

void checkFieldNames()
{
    constexpr Field unique[] = {{"meter"}, {"sensor"}, {""}};
    constexpr Field nullFirst[] = {{nullptr}, {"sensor"}};
    constexpr Field nullLater[] = {{"meter"}, {nullptr}};
    constexpr Field emptyNames[] = {{""}, {""}};
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
    const Field laterNullRows[] = {rows[0], rows[1], {nullptr}};
    expect(!telemetry::names_unique(laterNullRows, std::size(laterNullRows)), "a later null field name is rejected");
    const char separateName[] = {'m', 'e', 't', 'e', 'r', '\0'};
    const Field duplicateRows[] = {rows[0], rows[1], {separateName}};
    expect(!telemetry::names_unique(duplicateRows, std::size(duplicateRows)),
        "field name uniqueness compares text from different storage");
    expect(!telemetry::names_unique(emptyNames, std::size(emptyNames)),
        "duplicate empty field names are rejected");
}

void checkCatalogNames()
{
    constexpr Catalog unique[] = {{"meter", nullptr, 0}, {"sensor", nullptr, 0}};
    static_assert(telemetry::catalog_names_unique(unique, std::size(unique)));
    static_assert(telemetry::catalog_names_unique(nullptr, 0));
    expect(telemetry::catalog_names_unique(unique, 0) && telemetry::catalog_names_unique(unique, 1)
        && telemetry::catalog_names_unique(unique, std::size(unique)),
        "empty, single and distinct catalog names are accepted");

    const char separateName[] = {'m', 'e', 't', 'e', 'r', '\0'};
    const Catalog duplicate[] = {unique[0], {separateName, nullptr, 0}};
    expect(!telemetry::catalog_names_unique(duplicate, std::size(duplicate)),
        "catalog name uniqueness compares text from different storage");
    constexpr Catalog emptyNames[] = {{"", nullptr, 0}, {"", nullptr, 0}};
    static_assert(!telemetry::catalog_names_unique(emptyNames, std::size(emptyNames)));
    expect(!telemetry::catalog_names_unique(emptyNames, std::size(emptyNames)),
        "duplicate empty catalog names are rejected");
    constexpr Catalog nullName{nullptr, nullptr, 0};
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
