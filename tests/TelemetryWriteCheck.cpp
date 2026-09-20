#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

#include "serialization/TelemetryJson.h"
#include "tiny_delegate.hpp"

namespace {
using namespace telemetry;
int checks = 0;
int failures = 0;

void expect(bool ok, const char* message)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message);
}

bool same(const Scalar& a, const Scalar& b)
{
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case ScalarType::Null: return true;
        case ScalarType::F32: return a.get<float>() == b.get<float>() || (std::isnan(a.get<float>()) && std::isnan(b.get<float>()));
        case ScalarType::F64: return a.get<double>() == b.get<double>() || (std::isnan(a.get<double>()) && std::isnan(b.get<double>()));
        case ScalarType::U8: return a.get<std::uint8_t>() == b.get<std::uint8_t>();
        case ScalarType::U16: return a.get<std::uint16_t>() == b.get<std::uint16_t>();
        case ScalarType::U32: return a.get<std::uint32_t>() == b.get<std::uint32_t>();
        case ScalarType::U64: return a.get<std::uint64_t>() == b.get<std::uint64_t>();
        case ScalarType::S8: return a.get<std::int8_t>() == b.get<std::int8_t>();
        case ScalarType::S16: return a.get<std::int16_t>() == b.get<std::int16_t>();
        case ScalarType::S32: return a.get<std::int32_t>() == b.get<std::int32_t>();
        case ScalarType::S64: return a.get<std::int64_t>() == b.get<std::int64_t>();
        case ScalarType::Bool: return a.get<bool>() == b.get<bool>();
        default: return false;
    }
}

constexpr Scalar converted(Scalar value, ScalarType target)
{
    Scalar result;
    return convertScalar(value, target, result) ? result : Scalar::null();
}
static_assert(converted(250, ScalarType::F32).get<float>() == 250.0f);
static_assert(converted(12.7, ScalarType::U16).get<std::uint16_t>() == 12);
static_assert(converted(-12.7, ScalarType::S8).get<std::int8_t>() == -12);
static_assert(converted(-128.9, ScalarType::S8).get<std::int8_t>() == INT8_MIN);
static_assert(converted(-0.75, ScalarType::U8).get<std::uint8_t>() == 0);
static_assert(converted(0x1p64, ScalarType::U64).type() == ScalarType::Null);
static_assert(converted(0x1p63, ScalarType::S64).type() == ScalarType::Null);
static_assert(converted(-0x1p63, ScalarType::S64).get<std::int64_t>() == INT64_MIN);
static_assert(converted(UINT64_MAX, ScalarType::U64).get<std::uint64_t>() == UINT64_MAX);
static_assert(converted(UINT64_MAX, ScalarType::S64).type() == ScalarType::Null);
static_assert(!std::is_convertible_v<const char*, Scalar>);
static_assert(!std::is_convertible_v<void*, Scalar>);
static_assert(!std::is_convertible_v<long double, Scalar>);
static_assert(std::is_nothrow_constructible_v<Scalar, double>);

void expectConversion(Scalar input, Scalar expected, const char* message)
{
    Scalar output;
    expect(convertScalar(input, expected.type(), output) && same(output, expected), message);
}

void expectRejected(Scalar input, ScalarType target, const char* message)
{
    const Scalar sentinel = Scalar::fromS64(-17);
    Scalar output = sentinel;
    expect(!convertScalar(input, target, output) && same(output, sentinel), message);
}

void checkConversions()
{
    const Scalar ones[] = {1.0f, 1.0, std::uint8_t{1}, std::uint16_t{1}, std::uint32_t{1}, std::uint64_t{1},
                           std::int8_t{1}, std::int16_t{1}, std::int32_t{1}, std::int64_t{1}, true};
    bool allPairs = true;
    for (const auto& input : ones) {
        for (const auto& expected : ones) {
            Scalar output;
            allPairs = convertScalar(input, expected.type(), output) && same(output, expected) && allPairs;
        }
    }
    expect(allPairs, "all 121 numeric/bool source and target pairs convert one correctly");
    expectConversion(12.7, Scalar::fromU16(12), "positive fractions truncate toward zero");
    expectConversion(-12.7, Scalar::fromS8(-12), "negative fractions truncate toward zero");
    expectConversion(-0.75, Scalar::fromU8(0), "a negative fraction truncating to zero fits unsigned");
    expectConversion(-128.99, Scalar::fromS8(INT8_MIN), "fraction below signed minimum may truncate to the minimum");
    expectConversion(255.99, Scalar::fromU8(UINT8_MAX), "fraction above unsigned maximum may truncate to the maximum");
    expectRejected(-129.0, ScalarType::S8, "whole value below signed minimum is rejected");
    expectRejected(256.0, ScalarType::U8, "unsigned upper endpoint is exclusive");
    expectRejected(-1, ScalarType::U64, "negative integers never wrap into unsigned values");
    expectRejected(255u, ScalarType::S8, "narrow signed conversion rejects overflow");
    expectConversion(INT64_MAX, Scalar::fromU64(INT64_MAX), "signed maximum converts to unsigned exactly");
    expectConversion(UINT64_MAX, Scalar::fromU64(UINT64_MAX), "U64 maximum retains all bits");
    expectConversion(INT64_MIN, Scalar::fromS64(INT64_MIN), "S64 minimum retains all bits");
    expectRejected(UINT64_MAX, ScalarType::S64, "U64 maximum cannot enter S64");
    expectRejected(INT64_MIN, ScalarType::U64, "S64 minimum cannot enter U64");
    expectRejected(0x1p64, ScalarType::U64, "2^64 is rejected before floating-to-U64 cast");
    expectRejected(0x1p63, ScalarType::S64, "2^63 is rejected before floating-to-S64 cast");
    expectConversion(-0x1p63, Scalar::fromS64(INT64_MIN), "-2^63 is a valid S64 endpoint");
    expectConversion(std::nextafter(0x1p64, 0.0), Scalar::fromU64(UINT64_MAX - 2047),
                     "last double below 2^64 converts without rounding the integer bound");
    expectConversion(std::nextafter(0x1p63, 0.0), Scalar::fromS64(INT64_MAX - 1023),
                     "last double below 2^63 converts exactly");
    expectRejected(std::nextafter(-0x1p63, -INFINITY), ScalarType::S64,
                   "first double below -2^63 is rejected");
    expectConversion(std::nextafter(0x1p32, 0.0), Scalar::fromU32(UINT32_MAX), "U32 float endpoint truncates correctly");
    expectConversion(-2147483648.75, Scalar::fromS32(INT32_MIN), "S32 negative endpoint truncates correctly");
    expectRejected(-2147483649.0, ScalarType::S32, "S32 lower overflow is rejected");
    expectConversion(-12.7, Scalar::fromBool(true), "nonzero finite numbers convert to true");
    expectConversion(-0.0, Scalar::fromBool(false), "negative zero converts to false");
    expectConversion(false, Scalar::fromF64(0), "false converts to numeric zero");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double value : {nan, infinity, -infinity}) {
        expectRejected(value, ScalarType::U64, "non-finite input cannot enter U64");
        expectRejected(value, ScalarType::S64, "non-finite input cannot enter S64");
        expectRejected(value, ScalarType::Bool, "non-finite input cannot enter Bool");
    }
    expectConversion(nan, Scalar::fromF32(std::numeric_limits<float>::quiet_NaN()), "NaN remains available to float owners");
    expectConversion(INFINITY, Scalar::fromF64(INFINITY), "positive infinity remains available to float owners");
    expectConversion(-std::numeric_limits<double>::infinity(), Scalar::fromF32(-INFINITY), "negative infinity narrows to F32");
    const double floatLimit = std::numeric_limits<float>::max();
    expectConversion(floatLimit, Scalar::fromF32(std::numeric_limits<float>::max()), "largest finite F32 is accepted");
    expectRejected(std::nextafter(floatLimit, INFINITY), ScalarType::F32, "finite F64 above F32 range is rejected");
    expectRejected(-std::numeric_limits<double>::max(), ScalarType::F32, "negative F64 outside F32 range is rejected");
    expectConversion(std::numeric_limits<double>::denorm_min(), Scalar::fromF32(0), "tiny finite input may round to zero");
    expectRejected(Scalar::null(), ScalarType::U8, "Null cannot be written as a number");
    expectRejected(Scalar::null(), ScalarType::Null, "identity conversion does not make Null writable");
    expectRejected(1, ScalarType::Null, "Null is not a numeric destination");
    Scalar empty;
    expect(empty.type() == ScalarType::Null && empty.getIf<float>() == nullptr,
           "default variant storage has no numeric payload");
    empty = Scalar::fromU64(UINT64_MAX);
    expect(empty.type() == ScalarType::U64 && empty.get<std::uint64_t>() == UINT64_MAX
               && empty.getIf<float>() == nullptr,
           "variant assignment changes the tag and payload together");
    expectRejected(1, static_cast<ScalarType>(255), "unknown destination tags are rejected");
    Scalar alias = 12.7;
    expect(convertScalar(alias, ScalarType::U8, alias) && same(alias, Scalar::fromU8(12)), "conversion permits output aliasing input");
    expect(convertScalar(alias, ScalarType::U8, alias) && same(alias, Scalar::fromU8(12)), "identity conversion permits output aliasing input");
}

template <class T> T nativeValue = static_cast<T>(7);
template <class T> T nativeRead() noexcept { return nativeValue<T>; }

template <class T>
void checkNativeGetter(const char* name)
{
    constexpr Getter function = &nativeRead<T>;
    constexpr Getter bound = Getter::bind<&nativeRead<T>>();
    constexpr Getter bare = []() noexcept { return nativeRead<T>(); };
    constexpr Getter plus = +[]() noexcept { return nativeRead<T>(); };
    constexpr Getter explicitScalar = []() noexcept { return Scalar::from(nativeRead<T>()); };
    constexpr Getter typedReturn = []() noexcept -> Scalar { return nativeRead<T>(); };
    static_assert(function && bound && bare && plus && explicitScalar && typedReturn);
    Getter getters[] = {function, bound, bare, plus, explicitScalar, typedReturn};
    bool correct = true;
    for (const auto& getter : getters) correct = same(getter(), Scalar::from(nativeValue<T>)) && correct;
    nativeValue<T> = std::is_same_v<T, bool> ? static_cast<T>(false) : static_cast<T>(9);
    for (const auto& getter : getters) correct = same(getter(), Scalar::from(nativeValue<T>)) && correct;
    constexpr Getter empty{static_cast<T (*)() noexcept>(nullptr)};
    static_assert(!empty);
    correct = !empty && empty().type() == ScalarType::Null && correct;
    getters[0] = nullptr;
    getters[0] = &nativeRead<T>;
    const Getter copy = getters[0];
    correct = same(copy(), Scalar::from(nativeValue<T>)) && correct;
    expect(correct, name);
}

template <class T>
WriteResult nativeWrite(T value) noexcept
{
    nativeValue<T> = value;
    return WriteResult::Applied;
}

template <class T>
void checkNativeSetter(const char* name)
{
    constexpr Setter function = &nativeWrite<T>;
    constexpr Setter bare = [](T value) noexcept { return nativeWrite<T>(value); };
    constexpr Setter plus = +[](T value) noexcept { return nativeWrite<T>(value); };
    static_assert(function && bare && plus);
    constexpr T expected = std::is_same_v<T, bool> ? static_cast<T>(true) : static_cast<T>(11);
    const Scalar normalized = Scalar::from(expected);
    Setter setters[] = {function, bare, plus};
    bool correct = true;
    for (const auto& setter : setters) {
        nativeValue<T> = T{};
        correct = setter(normalized) == WriteResult::Applied
            && nativeValue<T> == expected && correct;
        correct = setter(Scalar::null()) == WriteResult::InvalidValue && correct;
    }
    constexpr Setter empty{static_cast<WriteResult (*)(T) noexcept>(nullptr)};
    static_assert(!empty);
    correct = empty(normalized) == WriteResult::ReadOnly && correct;
    expect(correct, name);
}

struct Owner {
    float value = 10.0f;
    int calls = 0;
    int reads = 0;
    bool busy = false;
    float read() noexcept { ++reads; return value; }
    WriteResult write(const Scalar& input) noexcept
    {
        ++calls;
        if (busy) return WriteResult::Busy;
        if (input.type() != ScalarType::F32 || !std::isfinite(input.get<float>())
            || input.get<float>() < 0 || input.get<float>() > 1000) return WriteResult::InvalidValue;
        value = input.get<float>();
        return WriteResult::Applied;
    }
};

struct BindingOwner {
    mutable float value = 1;
    float read() const noexcept { return value; }
    WriteResult write(const Scalar& input) const noexcept
    {
        value = input.get<float>();
        return WriteResult::Applied;
    }
};

template <class T, class Argument, class = void> struct CanExplicitGetter : std::false_type {};
template <class T, class Argument>
struct CanExplicitGetter<T, Argument, std::void_t<decltype(
    Getter::bind<&BindingOwner::read, T>(std::declval<Argument>()))>> : std::true_type {};
template <class T, class Argument, class = void> struct CanExplicitSetter : std::false_type {};
template <class T, class Argument>
struct CanExplicitSetter<T, Argument, std::void_t<decltype(
    Setter::bind<&BindingOwner::write, T>(std::declval<Argument>()))>> : std::true_type {};

static_assert(CanExplicitGetter<BindingOwner, BindingOwner&>::value);
static_assert(CanExplicitSetter<BindingOwner, BindingOwner&>::value);
static_assert(CanExplicitGetter<const BindingOwner, const BindingOwner&>::value);
static_assert(CanExplicitSetter<const BindingOwner, const BindingOwner&>::value);
static_assert(!CanExplicitGetter<const BindingOwner, BindingOwner&&>::value);
static_assert(!CanExplicitSetter<const BindingOwner, BindingOwner&&>::value);
static_assert(!CanExplicitGetter<const BindingOwner, const BindingOwner&&>::value);
static_assert(!CanExplicitSetter<const BindingOwner, const BindingOwner&&>::value);
static_assert(!CanExplicitGetter<const BindingOwner&, BindingOwner&&>::value);
static_assert(!CanExplicitSetter<const BindingOwner&, BindingOwner&&>::value);
static_assert(!CanExplicitGetter<BindingOwner&, BindingOwner&>::value);
static_assert(!CanExplicitSetter<BindingOwner&, BindingOwner&>::value);

const BindingOwner constantOwner;
constexpr auto constantGetter = Getter::bind<&BindingOwner::read, const BindingOwner>(constantOwner);
constexpr auto constantSetter = Setter::bind<&BindingOwner::write, const BindingOwner>(constantOwner);
static_assert(constantGetter && constantSetter);

Owner globalOwner;
WriteResult namedWrite(const Scalar& value) noexcept { return globalOwner.write(value); }
constexpr Setter bindingForms[] = {
    namedWrite, &namedWrite, Setter::bind<&namedWrite>(),
    [](const Scalar& value) noexcept { return namedWrite(value); },
    +[](const Scalar& value) noexcept { return namedWrite(value); },
    Setter::bind<&Owner::write>(globalOwner),
};
constexpr Setter emptySetter = nullptr;
constexpr Setter typedEmptySetter{static_cast<Setter::Function>(nullptr)};
static_assert(!emptySetter && !typedEmptySetter && bindingForms[5]);
static_assert(std::is_trivially_copyable_v<Setter> && std::is_trivially_copyable_v<Getter>);
static_assert(noexcept(emptySetter(Scalar::null())));
static_assert(!std::is_constructible_v<Setter, WriteResult (*)(const Scalar&)>);
static_assert(!std::is_constructible_v<Setter, bool (*)(const Scalar&) noexcept>);
static_assert(!std::is_constructible_v<Getter, float (*)()>);
static_assert(!std::is_constructible_v<Getter, Scalar (*)()>);

// A custom unary plus must not admit a delegate with unchecked target policy
// through the constructor intended for native-returning function pointers.
struct DelegateConversion {
    using Delegate = tiny::delegate_ref<Scalar()>;
    operator Delegate() const noexcept;
    Delegate operator+() const noexcept;
};
static_assert(!std::is_constructible_v<Getter, DelegateConversion>);

#if __cplusplus >= 202002L
// Class NTTPs denote const objects. The invoked const-lvalue overload must
// supply both the behavior and the noexcept contract, even when another
// ref-qualified overload or a function-pointer conversion is available.
struct StructuralOwner { float value = 7.0f; int reads = 0; int writes = 0; };
struct StructuralRead {
    float operator()(StructuralOwner& owner) const & noexcept
    { ++owner.reads; return owner.value; }
    float operator()(StructuralOwner&) const && { return -1.0f; }
};
struct StructuralWrite {
    WriteResult operator()(StructuralOwner& owner, const Scalar& value) const & noexcept
    { ++owner.writes; owner.value = value.get<float>(); return WriteResult::Applied; }
    WriteResult operator()(StructuralOwner&, const Scalar&) const && { return WriteResult::Busy; }
};
WriteResult structuralPointerTarget(const Scalar&) noexcept { return WriteResult::Busy; }
struct StructuralSetter {
    constexpr operator Setter::Function() const noexcept { return &structuralPointerTarget; }
    WriteResult operator()(const Scalar&) const & noexcept { return WriteResult::Applied; }
    WriteResult operator()(const Scalar&) const && { return WriteResult::Busy; }
};
constexpr Setter structuralSetter = Setter::bind<StructuralSetter{}>();
static_assert(structuralSetter);

void checkStructuralAdapters()
{
    StructuralOwner owner;
    const auto getter = Getter::bindContext<StructuralRead{}>(owner);
    const auto setter = Setter::bindContext<StructuralWrite{}>(owner);
    expect(getter().get<float>() == 7.0f && owner.reads == 1,
           "structural getter context uses the noexcept const-lvalue overload");
    expect(setter(Scalar::fromF32(23.0f)) == WriteResult::Applied
           && owner.value == 23.0f && owner.writes == 1,
           "structural setter context uses the noexcept const-lvalue overload");
    expect(structuralSetter(Scalar::fromF32(23.0f)) == WriteResult::Applied,
           "structural setter invokes its validated call operator rather than converted pointer");
}
#endif

template <class T, class = void> struct CanBindSetter : std::false_type {};
template <class T> struct CanBindSetter<T, std::void_t<decltype(Setter::bind<&Owner::write>(std::declval<T>()))>> : std::true_type {};
static_assert(CanBindSetter<Owner&>::value && !CanBindSetter<Owner&&>::value);
template <class T, class = void> struct CanWrite : std::false_type {};
template <class T> struct CanWrite<T, std::void_t<decltype(std::declval<const CatalogIndex&>().write(0, std::declval<T>()))>> : std::true_type {};
static_assert(CanWrite<int>::value && CanWrite<float>::value && CanWrite<double>::value && CanWrite<bool>::value && CanWrite<Scalar>::value);
static_assert(!CanWrite<const char*>::value && !CanWrite<void*>::value && !CanWrite<long double>::value);
static_assert(noexcept(std::declval<const CatalogIndex&>().write(0, 250)));

void checkBindingsAndWrites()
{
    auto throwingGetter = [] { return 1.0f; };
    auto capturedGetter = [value = 1.0f]() noexcept { return value; };
    auto throwingSetter = [](const Scalar&) { return WriteResult::Applied; };
    auto capturedSetter = [result = WriteResult::Applied](const Scalar&) noexcept { return result; };
    static_assert(!std::is_constructible_v<Getter, decltype(throwingGetter)>);
    static_assert(!std::is_constructible_v<Getter, decltype(capturedGetter)>);
    static_assert(!std::is_constructible_v<Setter, decltype(throwingSetter)>);
    static_assert(!std::is_constructible_v<Setter, decltype(capturedSetter)>);
    static_assert(!std::is_assignable_v<Setter&, decltype(throwingSetter)>);
    bool formsWork = true;
    for (const auto& setter : bindingForms) formsWork = setter(Scalar::fromF32(20)) == WriteResult::Applied && formsWork;
    expect(formsWork && globalOwner.calls == 6 && globalOwner.value == 20, "all constexpr setter binding forms dispatch once");
    expect(emptySetter(Scalar::null()) == WriteResult::ReadOnly && typedEmptySetter(Scalar::null()) == WriteResult::ReadOnly,
           "null and typed-null setters return ReadOnly safely");
    Setter assigned = bindingForms[5];
    assigned = static_cast<Setter::Function>(nullptr);
    expect(!assigned && assigned(Scalar::null()) == WriteResult::ReadOnly, "typed-null assignment empties a bound setter");
    assigned = [](const Scalar&) noexcept { return WriteResult::Busy; };
    Setter copied = assigned;
    assigned = nullptr;
    expect(!assigned && copied(Scalar::null()) == WriteResult::Busy, "setter copies retain their target independently");

    Owner owner;
    const Field rows[] = {
        {"threshold", "V", ScalarType::F32, Getter::bind<&Owner::read>(owner), Setter::bind<&Owner::write>(owner)},
        {"readonly", "V", ScalarType::F32, Getter::bind<&Owner::read>(owner)},
    };
    const Catalog groups[] = {{"config", rows}};
    const CatalogIndex registry{groups};
    expect(registry.write(0, 250) == WriteResult::Applied && owner.value == 250 && owner.calls == 1 && owner.reads == 0,
           "template write converts an ordinary int to F32 without calling the getter");
    expect(registry.write(0, 12.75) == WriteResult::Applied && owner.value == 12.75f,
           "template write converts double to the field's F32 type");
    expect(registry.write(0, true) == WriteResult::Applied && owner.value == 1,
           "template write accepts bool as a numeric input");
    expect(registry.write(0, Scalar::fromU16(9)) == WriteResult::Applied && owner.value == 9,
           "existing Scalar callers use the same conversion path");
    const int beforeRejected = owner.calls;
    expect(registry.write(1, std::numeric_limits<double>::max()) == WriteResult::ReadOnly,
           "read-only status takes precedence over conversion failure");
    expect(registry.write(makeId(1, 0), 1) == WriteResult::NotFound && registry.write(makeId(0, 2), 1) == WriteResult::NotFound,
           "missing groups and fields cannot invoke any setter");
    expect(registry.write(0, std::numeric_limits<double>::max()) == WriteResult::InvalidValue
               && registry.write(0, Scalar::null()) == WriteResult::InvalidValue
               && owner.calls == beforeRejected && owner.value == 9,
           "conversion failures leave the owner untouched and never invoke its setter");
    expect(registry.write(0, -1) == WriteResult::InvalidValue && owner.value == 9,
           "owner validates its semantic range after conversion");
    owner.busy = true;
    expect(registry.write(0, 27) == WriteResult::Busy && owner.value == 9, "Busy propagates from the owner");
    owner.busy = false;
    expect(rows[0].write(42u) == WriteResult::Applied && rows[0].get().get<float>() == 42,
           "direct Field writes and native member getters share the same source");
    const Field clippedRows[] = {rows[0], {"clipped", "V", ScalarType::F32, nullptr, rows[0].set}};
    const Catalog clipped[] = {{"clipped", clippedRows}, {"later", nullptr, 0}};
    const CatalogIndex clippedIndex{clipped};
    const int beforeClipped = owner.calls;
    // Exercise incoming runtime IDs. Constant missing IDs are checked separately
    // by IndexCodegen; GCC 13 diagnoses unreachable array accesses when it loses
    // the constructor's prefix bounds while inlining these local fixtures.
    volatile FieldId missingLocal = makeId(0, 3);
    volatile FieldId missingGroup = makeId(2, 0);
    expect(clippedIndex.write(makeId(0, 2), 1) == WriteResult::NotFound
               && clippedIndex.write(missingLocal, 1) == WriteResult::NotFound
               && clippedIndex.write(missingGroup, 1) == WriteResult::NotFound && owner.calls == beforeClipped,
           "writes respect positional bounds");

    char schema[512];
    expect(writeSchema(registry, schema, sizeof(schema)) != 0
               && std::strstr(schema, "\"t\":\"f32\",\"w\":true")
               && std::strstr(schema, "\"t\":\"f32\",\"w\":false"),
           "schema distinguishes writable and read-only fields");
    const Field readonly[] = {{rows[0].name, rows[0].unit, rows[0].declaredType, rows[0].get, nullptr}};
    const Catalog readonlyGroup{"config", readonly};
    const Catalog writableGroup{"config", rows, 1};
    expect(schemaCrc(&readonlyGroup, 1) != schemaCrc(&writableGroup, 1), "setter presence changes the schema fingerprint");

    Scalar captured;
    struct Sink { Scalar& value; WriteResult write(const Scalar& next) const noexcept { value = next; return WriteResult::Applied; } };
    const Sink sink{captured};
    const Field byte{"byte", "", ScalarType::U8, nullptr, Setter::bind<&Sink::write>(sink)};
    expect(byte.write(12.7) == WriteResult::Applied && same(captured, Scalar::fromU8(12)),
           "fractional template write reaches a const-bound owner as the declared U8 type");
    expect(byte.write(1000) == WriteResult::InvalidValue && same(captured, Scalar::fromU8(12)), "out-of-range byte writes do not truncate bits");
}

void checkLifetimeContracts()
{
    BindingOwner owner;
    const auto get = Getter::bind<&BindingOwner::read, BindingOwner>(owner);
    const auto set = Setter::bind<&BindingOwner::write, BindingOwner>(owner);
    expect(set(13.0f) == WriteResult::Applied && get().get<float>() == 13,
           "explicit non-reference object types retain valid lvalue bindings");
    expect(constantSetter(17.0f) == WriteResult::Applied && constantGetter().get<float>() == 17,
           "constexpr explicit const object bindings keep live mutable state");

    struct Prefix { std::uint64_t sentinel = UINT64_MAX; };
    struct Derived : Prefix, BindingOwner {} derived;
    const auto derivedGet = Getter::bind<&BindingOwner::read>(derived);
    const auto derivedSet = Setter::bind<&BindingOwner::write>(derived);
    expect(derivedSet(23.0f) == WriteResult::Applied && derivedGet().get<float>() == 23
        && derived.sentinel == UINT64_MAX,
        "member bindings preserve base adjustment in a multiply inherited owner");

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    struct ThrowingGetterConversion {
        operator Getter::Function() const { throw 1; }
    };
    struct ThrowingSetterConversion {
        operator Setter::Function() const { throw 2; }
    };
    static_assert(!std::is_nothrow_constructible_v<Getter, ThrowingGetterConversion>);
    static_assert(!std::is_nothrow_constructible_v<Setter, ThrowingSetterConversion>);
    Getter preservedGetter = get;
    Setter preservedSetter = set;
    bool getterThrew = false, setterThrew = false;
    try { preservedGetter = ThrowingGetterConversion{}; } catch (int) { getterThrew = true; }
    try { preservedSetter = ThrowingSetterConversion{}; } catch (int) { setterThrew = true; }
    expect(getterThrew && preservedGetter().get<float>() == 13,
           "failed function-pointer conversion preserves the old getter binding");
    expect(setterThrew && preservedSetter(29.0f) == WriteResult::Applied && owner.value == 29,
           "failed function-pointer conversion preserves the old setter binding");
#endif
}

} // namespace

int main()
{
    checkConversions();
    checkNativeGetter<bool>("native bool getters");
    checkNativeGetter<char>("native char getters");
    checkNativeGetter<signed char>("native signed char getters");
    checkNativeGetter<unsigned char>("native unsigned char getters");
    checkNativeGetter<short>("native short getters");
    checkNativeGetter<unsigned short>("native unsigned short getters");
    checkNativeGetter<int>("native int getters");
    checkNativeGetter<unsigned>("native unsigned getters");
    checkNativeGetter<long>("native long getters");
    checkNativeGetter<unsigned long>("native unsigned long getters");
    checkNativeGetter<long long>("native long long getters");
    checkNativeGetter<unsigned long long>("native unsigned long long getters");
    checkNativeGetter<float>("native float getters");
    checkNativeGetter<double>("native double getters");
    checkNativeGetter<wchar_t>("native wchar_t getters");
    checkNativeGetter<char16_t>("native char16_t getters");
    checkNativeGetter<char32_t>("native char32_t getters");
#ifdef __cpp_char8_t
    checkNativeGetter<char8_t>("native char8_t getters");
#endif
    checkNativeSetter<bool>("native bool setters");
    checkNativeSetter<char>("native char setters");
    checkNativeSetter<signed char>("native signed char setters");
    checkNativeSetter<unsigned char>("native unsigned char setters");
    checkNativeSetter<short>("native short setters");
    checkNativeSetter<unsigned short>("native unsigned short setters");
    checkNativeSetter<int>("native int setters");
    checkNativeSetter<unsigned>("native unsigned setters");
    checkNativeSetter<long>("native long setters");
    checkNativeSetter<unsigned long>("native unsigned long setters");
    checkNativeSetter<long long>("native long long setters");
    checkNativeSetter<unsigned long long>("native unsigned long long setters");
    checkNativeSetter<float>("native float setters");
    checkNativeSetter<double>("native double setters");
    checkNativeSetter<wchar_t>("native wchar_t setters");
    checkNativeSetter<char16_t>("native char16_t setters");
    checkNativeSetter<char32_t>("native char32_t setters");
#ifdef __cpp_char8_t
    checkNativeSetter<char8_t>("native char8_t setters");
#endif
    checkBindingsAndWrites();
    checkLifetimeContracts();
#if __cplusplus >= 202002L
    checkStructuralAdapters();
#endif
    std::printf("%d/%d write and native getter/setter checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
