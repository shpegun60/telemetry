#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "serialization/TelemetryJson.h"

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
    Scalar value = 12.75f;
    int reads = 0;
    int writes = 0;
    Scalar read() noexcept { ++reads; return value; }
    WriteResult write(const Scalar& next) noexcept { ++writes; value = next; return WriteResult::Applied; }
};
Source source;

constexpr Field fields[] = {
    {makeId(0, 0), "Float", "", ScalarType::F32, Getter::bind<&Source::read>(source), Setter::bind<&Source::write>(source)},
    {makeId(0, 1), "Double", "", ScalarType::F64, []() noexcept { return 14.5; }},
    {makeId(0, 2), "U8", "", ScalarType::U8, []() noexcept { return std::uint8_t{UINT8_MAX}; }},
    {makeId(0, 3), "U16", "", ScalarType::U16, []() noexcept { return std::uint16_t{UINT16_MAX}; }},
    {makeId(0, 4), "U32", "", ScalarType::U32, []() noexcept { return std::uint32_t{UINT32_MAX}; }},
    {makeId(0, 5), "U64", "", ScalarType::U64, []() noexcept { return std::uint64_t{UINT64_MAX}; }},
    {makeId(0, 6), "S8", "", ScalarType::S8, []() noexcept { return std::int8_t{INT8_MIN}; }},
    {makeId(0, 7), "S16", "", ScalarType::S16, []() noexcept { return std::int16_t{INT16_MIN}; }},
    {makeId(0, 8), "S32", "", ScalarType::S32, []() noexcept { return std::int32_t{INT32_MIN}; }},
    {makeId(0, 9), "S64", "", ScalarType::S64, []() noexcept { return std::int64_t{INT64_MIN}; }},
    {makeId(0, 10), "Bool", "", ScalarType::Bool, []() noexcept { return false; }},
    {makeId(0, 11), "Empty", "", ScalarType::F32},
    {makeId(0, 12), "Unavailable", "", ScalarType::F32, []() noexcept { return Scalar::null(); }},
};
constexpr Catalog catalogs[] = {{0, "values", fields}};
constexpr CatalogIndex runtime{catalogs};
constexpr auto fixed = CatalogIndex::bind<catalogs>();

static_assert(fixed.size() == 1 && fixed.catalog(0)->count == 13);
static_assert(fixed.find(0) == &fields[0] && fixed.data() == catalogs);
static_assert(fixed.catalog(1) == nullptr);
static_assert(std::is_empty_v<decltype(fixed)>);
static_assert(std::is_same_v<decltype(fields[0].read()), Scalar>);
static_assert(std::is_same_v<decltype(runtime.read(0)), Scalar>);
static_assert(std::is_same_v<decltype(fixed.read(0)), Scalar>);
static_assert(noexcept(fields[0].read()) && noexcept(fields[0].read<float>()));
static_assert(noexcept(runtime.read<float>(0)) && noexcept(fixed.read<0>())
              && noexcept(fixed.read<0, double>()) && noexcept(fixed.write<0>(1.0f)));
static_assert(std::is_same_v<decltype(fixed.read<0, double>()), std::optional<double>>);
static_assert(std::is_same_v<decltype(fixed.write<0>(1.0f)), WriteResult>);
static_assert(convertScalar<std::uint16_t>(Scalar::fromF64(12.7)).value() == 12);
static_assert(!convertScalar<std::uint64_t>(Scalar::fromF64(0x1p64)));
static_assert(std::is_same_v<decltype(Scalar::fromF32(1).get<float>()), float>);
static_assert(!std::is_assignable_v<decltype(std::declval<Scalar&>().type()), ScalarType>);
static_assert(!std::is_constructible_v<Scalar, ScalarType>);

template <class T, class = void> struct CanBorrowScalar : std::false_type {};
template <class T> struct CanBorrowScalar<T, std::void_t<decltype(std::declval<T>().template getIf<float>())>> : std::true_type {};
static_assert(CanBorrowScalar<Scalar&>::value && CanBorrowScalar<const Scalar&>::value);
static_assert(!CanBorrowScalar<Scalar&&>::value && !CanBorrowScalar<const Scalar&&>::value);

template <class T, class = void> struct CanReadField : std::false_type {};
template <class T> struct CanReadField<T, std::void_t<decltype(fields[0].template read<T>())>> : std::true_type {};
template <class T, class = void> struct CanReadIndex : std::false_type {};
template <class T> struct CanReadIndex<T, std::void_t<decltype(runtime.template read<T>(0)),
                                                  decltype(fixed.template read<T>(0))>> : std::true_type {};
struct Incomplete;
enum class AnEnum { Value };
template <class T> constexpr bool rejected = !CanReadField<T>::value && !CanReadIndex<T>::value;
static_assert(rejected<void> && rejected<void()> && rejected<Incomplete>);
static_assert(rejected<float&> && rejected<const int> && rejected<volatile float>);
static_assert(rejected<Scalar> && rejected<void*> && rejected<const char*>);
static_assert(rejected<long double> && rejected<AnEnum>);

template <FieldId Id, class T>
void checkInferred(T expected, const char* message)
{
    static_assert(std::is_same_v<decltype(fixed.read<Id>()), std::optional<T>>);
    static_assert(std::is_same_v<decltype(fixed.read<Id, T>()), std::optional<T>>);
    const auto inferred = fixed.read<Id>();
    const auto compileTimeExplicit = fixed.read<Id, T>();
    const auto explicitType = runtime.read<T>(Id);
    expect(inferred && compileTimeExplicit && explicitType
               && *inferred == expected && *compileTimeExplicit == expected
               && *explicitType == expected,
           message);
}

template <class T>
void checkNativeType(const char* message)
{
    constexpr Field field{0, "value", "", ScalarType::U8, []() noexcept { return std::uint8_t{7}; }};
    static_assert(CanReadField<T>::value && CanReadIndex<T>::value);
    const auto result = field.read<T>();
    expect(result && *result == static_cast<T>(7), message);
}

template <class To, class From>
void checkFloatingBounds(const char* message)
{
    Source owner;
    const Field field{0, "boundaries", "", Scalar::from(To{}).type(),
        Getter::bind<&Source::read>(owner), Setter::bind<&Source::write>(owner)};
    const From lower = static_cast<From>(std::numeric_limits<To>::lowest());
    const From upper = static_cast<From>(std::numeric_limits<To>::max() / 2 + 1) * From{2};
    const From infinity = std::numeric_limits<From>::infinity();
    const From belowLower = std::nextafter(lower, -infinity);
    const From values[] = {lower, belowLower, upper, std::nextafter(upper, -infinity),
                           infinity, -infinity, std::numeric_limits<From>::quiet_NaN()};
    // std::trunc supplies an independent reference for the value immediately
    // below min(): it may truncate to min(), or already be out of range.
    const bool accepted[] = {true, std::trunc(belowLower) == lower, false, true, false, false, false};
    bool correct = true;
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        const Scalar input = Scalar::from(values[i]);
        const auto typed = convertScalar<To>(input);
        Scalar boxed = Scalar::fromBool(true);
        const bool converted = convertScalar(input, Scalar::from(To{}).type(), boxed);
        correct = typed.has_value() == accepted[i] && converted == accepted[i] && correct;
        owner.value = input;
        owner.reads = owner.writes = 0;
        const auto declared = field.read();
        const auto asOriginalType = field.read<From>();
        correct = asOriginalType.has_value() == accepted[i] && owner.reads == 2
                  && owner.writes == 0 && correct;
        const auto written = field.write(values[i]);
        correct = written == (accepted[i] ? WriteResult::Applied : WriteResult::InvalidValue)
                  && owner.writes == (accepted[i] ? 1 : 0) && owner.reads == 2 && correct;
        if (accepted[i]) {
            const auto unboxed = convertScalar<To>(boxed);
            correct = typed && unboxed && *typed == *unboxed
                      && static_cast<From>(*typed) == std::trunc(values[i]) && correct;
            const auto fieldValue = declared.getIf<To>();
            const auto setterValue = owner.value.getIf<To>();
            correct = typed && fieldValue && setterValue && *fieldValue == *typed && *setterValue == *typed
                      && asOriginalType && *asOriginalType == std::trunc(values[i]) && correct;
        } else {
            correct = boxed.type() == ScalarType::Bool && boxed.get<bool>()
                      && declared.type() == ScalarType::Null && correct;
        }
    }
    expect(correct, message);
}

void checkIdentityPayload()
{
    const std::uint32_t bits = 0x7fc12345u;
    float number;
    std::memcpy(&number, &bits, sizeof(number));
    const Scalar input = Scalar::fromF32(number);
    Scalar boxed;
    const bool converted = convertScalar(input, ScalarType::F32, boxed);
    const auto typed = convertScalar<float>(input);
    std::uint32_t boxedBits = 0, typedBits = 0;
    if (const auto* value = boxed.getIf<float>()) std::memcpy(&boxedBits, value, sizeof(boxedBits));
    if (typed) std::memcpy(&typedBits, &*typed, sizeof(typedBits));
    expect(converted && typed && boxedBits == bits && typedBits == bits,
           "identity paths preserve the original F32 NaN payload on this platform");

    Source owner;
    owner.value = input;
    const Field field{0, "identity", "", ScalarType::F32,
        Getter::bind<&Source::read>(owner), Setter::bind<&Source::write>(owner)};
    const auto read = field.read();
    std::uint32_t readBits = 0, writeBits = 0;
    if (const auto* value = read.getIf<float>()) std::memcpy(&readBits, value, sizeof(readBits));
    const auto written = field.write(input);
    if (const auto* value = owner.value.getIf<float>()) std::memcpy(&writeBits, value, sizeof(writeBits));
    expect(written == WriteResult::InvalidValue && readBits == bits && writeBits == bits
               && owner.reads == 1 && owner.writes == 0,
           "F32 reads preserve NaN payload; finite write bounds reject it before the setter");

    owner.value = -0.0f;
    const auto signedZero = field.read<float>();
    expect(signedZero && std::signbit(*signedZero), "matching F32 read preserves negative zero");
}

void checkDeclaredTypes()
{
    Source owner;
    const Field byte{0, "byte", "", ScalarType::U8,
        Getter::bind<&Source::read>(owner), Setter::bind<&Source::write>(owner)};
    const Field floating{0, "float", "", ScalarType::F32,
        Getter::bind<&Source::read>(owner), Setter::bind<&Source::write>(owner)};
    const Field boolean{0, "bool", "", ScalarType::Bool, Getter::bind<&Source::read>(owner)};
    const Field nullType{0, "null", "", ScalarType::Null, Getter::bind<&Source::read>(owner)};
    const Field unknownType{0, "unknown", "", static_cast<ScalarType>(255), Getter::bind<&Source::read>(owner)};

    owner.value = 12.75;
    expect(byte.read<double>() == 12.0 && byte.read<float>() == 12.0f,
           "explicit wider reads retain the declared integer's truncation");
    expect(byte.write(12.75) == WriteResult::Applied && owner.value.type() == ScalarType::U8
               && owner.value.get<std::uint8_t>() == 12,
           "the setter receives the same declared integer normalization");
    owner.value = 256.0;
    expect(byte.read().type() == ScalarType::Null && !byte.read<double>() && !byte.read<unsigned>(),
           "a wider requested type cannot bypass the declared U8 range");
    owner.value = std::uint64_t{16777217};
    expect(floating.read<double>() == 16777216.0 && floating.read<std::uint64_t>() == std::uint64_t{16777216},
           "typed reads retain declared F32 rounding even when the source is a U64");
    owner.value = std::numeric_limits<double>::max();
    expect(floating.read().type() == ScalarType::Null && !floating.read<double>(),
           "double readers cannot bypass finite overflow of declared F32");
    owner.value = 12.75;
    expect(boolean.read<double>() == 1.0 && boolean.read<int>() == 1,
           "the declared Bool type normalizes a nonzero number before a numeric read");
    owner.value = std::numeric_limits<double>::infinity();
    expect(!boolean.read<double>() && boolean.read().type() == ScalarType::Null,
           "declared Bool rejects infinity even for a floating reader");
    const auto infinity = floating.read<float>();
    expect(infinity && std::isinf(*infinity), "declared F32 preserves infinity from a double getter");
    owner.value = 3;
    expect(nullType.read().type() == ScalarType::Null && !nullType.read<int>()
               && unknownType.read().type() == ScalarType::Null && !unknownType.read<int>(),
           "Null and unknown metadata cannot publish a numeric getter value");

    const Scalar ones[] = {1.0f, 1.0, std::uint8_t{1}, std::uint16_t{1}, std::uint32_t{1}, std::uint64_t{1},
                           std::int8_t{1}, std::int16_t{1}, std::int32_t{1}, std::int64_t{1}, true};
    bool allPairs = true;
    for (const auto& input : ones) {
        for (const auto& expected : ones) {
            owner.value = input;
            owner.reads = owner.writes = 0;
            const Field field{0, "pair", "", expected.type(),
                Getter::bind<&Source::read>(owner), Setter::bind<&Source::write>(owner)};
            const auto normalized = field.read();
            const auto written = field.write(input);
            allPairs = normalized.type() == expected.type() && owner.value.type() == expected.type()
                       && convertScalar<double>(normalized) == 1.0 && convertScalar<double>(owner.value) == 1.0
                       && written == WriteResult::Applied && owner.reads == 1 && owner.writes == 1 && allPairs;
        }
    }
    expect(allPairs, "all 121 source/declared pairs have the same read and write normalization");

    constexpr Field native[] = {
        {makeId(0, 0), "bare", "", ScalarType::F32, []() noexcept { return 12.75; }},
        {makeId(0, 1), "plus", "", ScalarType::U16, +[]() noexcept { return 12.75f; }},
        {makeId(0, 2), "wide", "", ScalarType::F64, []() noexcept { return std::uint16_t{7}; }},
    };
    const Catalog group{0, "normalized", native};
    char json[128];
    expect(native[0].read<float>() == 12.75f && native[1].read<double>() == 12.0
               && native[2].read<double>() == 7.0,
           "native bare and plus lambda getters normalize to the declared type");
    expect(writeValues(&group, 1, json, sizeof(json)) != 0
               && std::strcmp(json, "{\"normalized\":[12.75,12,7]}") == 0,
           "JSON publishes normalized values with the same declared types");
}

void checkVariantStorage()
{
    Scalar value = 3.25f;
    expect(value.type() == ScalarType::F32 && value.getIf<float>() != nullptr
               && *value.getIf<float>() == 3.25f && value.getIf<double>() == nullptr,
           "variant inspection exposes only the active type");
    value = Scalar::fromU64(UINT64_MAX);
    expect(value.type() == ScalarType::U64 && value.get<std::uint64_t>() == UINT64_MAX
               && value.getIf<float>() == nullptr,
           "changing an alternative updates its type and value together");
    expect(Scalar::fromBool(false).get<bool>() == false,
           "get on a temporary returns a value rather than a dangling reference");
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    bool rejected = false;
    try { (void) value.get<float>(); }
    catch (const std::bad_variant_access&) { rejected = true; }
    expect(rejected, "a wrong exact-type get has a defined bad_variant_access failure");
#endif
}

void checkAccess()
{
    source.reads = 0;
    const auto raw = fields[0].read();
    expect(raw.type() == ScalarType::F32 && raw.get<float>() == 12.75f && source.reads == 1,
           "Scalar Field read calls its getter exactly once");
    const auto integer = fields[0].read<std::uint16_t>();
    expect(integer && *integer == 12 && source.reads == 2 && source.writes == 0,
           "typed Field read converts once without invoking the setter");
    const auto wide = fixed.read<double>(makeId(0, 0));
    expect(wide && *wide == 12.75 && source.reads == 3,
           "statically bound index also accepts explicit types and runtime IDs");
    const auto compileTimeWide = fixed.read<makeId(0, 0), double>();
    expect(compileTimeWide && *compileTimeWide == 12.75 && source.reads == 4,
           "compile-time IDs also accept an explicit requested representation");
    const auto rawIndex = fixed.read(0);
    expect(rawIndex.type() == ScalarType::F32 && rawIndex.get<float>() == 12.75f && source.reads == 5,
           "statically bound index retains normalized Scalar access");
    expect(runtime.read(65536).type() == ScalarType::Null && !runtime.read<float>(65536)
               && fixed.read(65536).type() == ScalarType::Null && !fixed.read<float>(65536)
               && source.reads == 5,
           "missing IDs produce no value without calling any source");
    const CatalogIndex empty;
    expect(empty.read(0).type() == ScalarType::Null && !empty.read<float>(0)
               && Field{}.read().type() == ScalarType::Null && !Field{}.read<float>(),
           "default fields and empty indexes are safe to read");
    expect(!fixed.read<11>() && !fixed.read<12>() && !runtime.read<float>(11)
               && fields[11].read().type() == ScalarType::Null,
           "empty and unavailable getters yield empty typed results");
    const auto validFalse = fixed.read<10>();
    expect(validFalse.has_value() && !*validFalse, "a valid false is distinct from a missing value");

    source.value = 0.0f;
    const auto zero = fixed.read<0>();
    expect(zero && *zero == 0.0f, "a valid zero is distinct from a missing value");
    source.value = -0.75f;
    const auto unsignedZero = runtime.read<std::uint8_t>(0);
    expect(unsignedZero && *unsignedZero == 0, "typed reads share truncation toward zero with writes");
    source.value = -1.0f;
    source.reads = 0;
    expect(!fields[0].read<std::uint64_t>() && source.reads == 1,
           "failed conversion still invokes the getter only once");
    source.value = 0x1p64f;
    expect(!fixed.read<std::uint64_t>(0), "typed read rejects 2^64 before an integer cast");
    expect(!fixed.read<std::int64_t>(5) && !fixed.read<std::uint64_t>(9),
           "integer endpoint conversions never wrap or pass through double");
    source.value = std::numeric_limits<float>::quiet_NaN();
    const auto nan = fixed.read<0>();
    expect(nan && std::isnan(*nan) && !fixed.read<bool>(0) && !fixed.read<int>(0),
           "NaN is retained for float readers and refused for bool and integer readers");
    source.value = std::numeric_limits<float>::infinity();
    const auto infinity = fixed.read<double>(0);
    expect(infinity && std::isinf(*infinity) && !fixed.read<int>(0),
           "infinity is retained only for floating destinations");

    source.value = Scalar::fromU16(7);
    const auto mismatched = fields[0].read();
    expect(mismatched.type() == ScalarType::F32 && mismatched.get<float>() == 7.0f,
           "Scalar access normalizes a U16 getter to the declared F32 type");
    expect(fields[0].read<float>() == 7.0f && runtime.read<float>(0) == 7.0f && fixed.read<0>() == 7.0f,
           "Field, runtime and inferred reads all honor declaredType");
    source.value = Scalar::null();
    expect(!fixed.read<0>() && runtime.read(0).type() == ScalarType::Null,
           "a previously available source may become unavailable");
    expect(source.value.getIf<float>() == nullptr, "Null has no floating alternative to read");
    const Field invalid{0, "invalid", "", static_cast<ScalarType>(255), Getter::bind<&Source::read>(source)};
    expect(!invalid.read<float>(), "unknown field metadata cannot expose a mismatched value");
    expect(fixed.write(0, 250) == WriteResult::Applied && source.writes == 1,
           "the same bound index supports numeric writes");
    const auto changed = fixed.read<0>();
    expect(changed && *changed == 250.0f, "inferred reads observe live source changes");
    expect(fixed.write<0>(251.5) == WriteResult::Applied && source.writes == 2
               && fixed.read<0>() == 251.5f,
           "compile-time write IDs deduce and normalize the input type");
    expect(fixed.write<1>(15.0) == WriteResult::ReadOnly && source.writes == 2,
           "compile-time writes preserve the runtime ReadOnly result");

    char schema[2048];
    char values[512];
    expect(writeSchema(fixed, schema, sizeof(schema)) != 0
               && writeValues(fixed, values, sizeof(values)) != 0
               && std::strstr(values, "18446744073709551615") != nullptr,
           "a bound index works with existing serializers and exact U64 output");

    constexpr Catalog clipped[] = {{0, "first", fields}, {2, "unreachable", fields}};
    const CatalogIndex prefix{clipped};
    volatile FieldId later = makeId(2, 0);
    const int before = source.reads;
    expect(!prefix.read<float>(later) && prefix.read(later).type() == ScalarType::Null && source.reads == before,
           "read methods honor the same accepted group prefix as lookup");
}
} // namespace

int main()
{
    checkInferred<0>(12.75f, "inferred F32");
    checkInferred<1>(14.5, "inferred F64");
    checkInferred<2>(std::uint8_t{UINT8_MAX}, "inferred U8");
    checkInferred<3>(std::uint16_t{UINT16_MAX}, "inferred U16");
    checkInferred<4>(std::uint32_t{UINT32_MAX}, "inferred U32");
    checkInferred<5>(std::uint64_t{UINT64_MAX}, "inferred exact U64");
    checkInferred<6>(std::int8_t{INT8_MIN}, "inferred S8");
    checkInferred<7>(std::int16_t{INT16_MIN}, "inferred S16");
    checkInferred<8>(std::int32_t{INT32_MIN}, "inferred S32");
    checkInferred<9>(std::int64_t{INT64_MIN}, "inferred exact S64");
    checkInferred<10>(false, "inferred Bool");
    checkNativeType<bool>("explicit bool");
    checkNativeType<char>("explicit char");
    checkNativeType<signed char>("explicit signed char");
    checkNativeType<unsigned char>("explicit unsigned char");
    checkNativeType<short>("explicit short");
    checkNativeType<unsigned short>("explicit unsigned short");
    checkNativeType<int>("explicit int");
    checkNativeType<unsigned>("explicit unsigned");
    checkNativeType<long>("explicit long");
    checkNativeType<unsigned long>("explicit unsigned long");
    checkNativeType<long long>("explicit long long");
    checkNativeType<unsigned long long>("explicit unsigned long long");
    checkNativeType<float>("explicit float");
    checkNativeType<double>("explicit double");
    checkNativeType<wchar_t>("explicit wchar_t");
    checkNativeType<char16_t>("explicit char16_t");
    checkNativeType<char32_t>("explicit char32_t");
#ifdef __cpp_char8_t
    checkNativeType<char8_t>("explicit char8_t");
#endif
    checkFloatingBounds<std::uint8_t, float>("F32 -> U8 endpoints");
    checkFloatingBounds<std::uint16_t, float>("F32 -> U16 endpoints");
    checkFloatingBounds<std::uint32_t, float>("F32 -> U32 endpoints");
    checkFloatingBounds<std::uint64_t, float>("F32 -> U64 endpoints");
    checkFloatingBounds<std::int8_t, float>("F32 -> S8 endpoints");
    checkFloatingBounds<std::int16_t, float>("F32 -> S16 endpoints");
    checkFloatingBounds<std::int32_t, float>("F32 -> S32 endpoints");
    checkFloatingBounds<std::int64_t, float>("F32 -> S64 endpoints");
    checkFloatingBounds<std::uint8_t, double>("F64 -> U8 endpoints");
    checkFloatingBounds<std::uint16_t, double>("F64 -> U16 endpoints");
    checkFloatingBounds<std::uint32_t, double>("F64 -> U32 endpoints");
    checkFloatingBounds<std::uint64_t, double>("F64 -> U64 endpoints");
    checkFloatingBounds<std::int8_t, double>("F64 -> S8 endpoints");
    checkFloatingBounds<std::int16_t, double>("F64 -> S16 endpoints");
    checkFloatingBounds<std::int32_t, double>("F64 -> S32 endpoints");
    checkFloatingBounds<std::int64_t, double>("F64 -> S64 endpoints");
    checkIdentityPayload();
    checkVariantStorage();
    checkAccess();
    checkDeclaredTypes();
    std::printf("%d/%d read checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
