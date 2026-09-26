// Review probe (numeric-core): hand-derived edge expectations for the Scalar
// conversion layer, checked at runtime through every public entry point and,
// where the value is a literal, at compile time as well.
// Build (repo root): g++ -std=c++17 -Wall -Wextra -Werror -pedantic-errors
//   -Ilib/telemetry -Ilib/delegate -O2 tests/review/numeric-core/ConversionEdges.cpp
#include "catalog/TelemetryIndex.h"
#include "field/TelemetryEnum.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace telemetry;

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL  %s\n", what); }
}

template <class T>
bool same(T a, T b)
{
    if constexpr (std::is_floating_point_v<T>)
        return (std::isnan(a) && std::isnan(b)) || (a == b && std::signbit(a) == std::signbit(b));
    else return a == b;
}

// Check convertScalar<T>, the runtime-target overload, and a Field write/read
// through a declared type of T, against one expectation.
struct Sink {
    Scalar value;
    int writes = 0;
    Scalar read() const noexcept { return value; }
    WriteResult write(const Scalar& next) noexcept { ++writes; value = next; return WriteResult::Applied; }
};

template <class To>
void check(const char* label, Scalar input, std::optional<To> expected)
{
    const auto typed = convertScalar<To>(input);
    char message[256];
    std::snprintf(message, sizeof message, "%s: convertScalar<T>", label);
    expect(typed.has_value() == expected.has_value() && (!typed || same(*typed, *expected)), message);

    Scalar out = Scalar::fromS64(-37);
    const bool ok = convertScalar(input, Scalar::from(To{}).type(), out);
    std::snprintf(message, sizeof message, "%s: convertScalar(target)", label);
    expect(ok == expected.has_value()
           && (ok ? (out.getIf<To>() && same(*out.getIf<To>(), *expected))
                  : out.get<std::int64_t>() == -37), message);

    Sink sink{input};
    const Field field{"v", "", Scalar::from(To{}).type(),
        Getter::bind<&Sink::read>(sink), Setter::bind<&Sink::write>(sink)};
    const Scalar read = field.read();
    std::snprintf(message, sizeof message, "%s: Field::read", label);
    expect(expected ? (read.getIf<To>() && same(*read.getIf<To>(), *expected))
                    : read.type() == ScalarType::Null, message);
    bool writable = expected.has_value();
    if constexpr (std::is_floating_point_v<To>) writable = writable && std::isfinite(*expected);
    const auto result = field.write(input);
    std::snprintf(message, sizeof message, "%s: Field::write", label);
    expect(result == (writable ? WriteResult::Applied : WriteResult::InvalidValue)
           && sink.writes == (writable ? 1 : 0), message);
}

constexpr double two63 = 9223372036854775808.0;
constexpr double two64 = 18446744073709551616.0;
constexpr float two63f = 9223372036854775808.0f;
constexpr float two64f = 18446744073709551616.0f;

// Compile-time evaluation of the same primitives (constexpr/runtime parity).
static_assert(!convertScalar<std::int64_t>(Scalar::fromF64(two63)));
static_assert(*convertScalar<std::int64_t>(Scalar::fromF64(-two63)) == INT64_MIN);
static_assert(!convertScalar<std::uint64_t>(Scalar::fromF64(two64)));
static_assert(*convertScalar<std::uint64_t>(Scalar::fromF64(two64 - 4096.0)) == 18446744073709547520ull);
static_assert(*convertScalar<std::uint32_t>(Scalar::fromF64(4294967295.5)) == 4294967295u);
static_assert(!convertScalar<std::uint32_t>(Scalar::fromF64(4294967296.0)));
static_assert(*convertScalar<std::uint16_t>(Scalar::fromF64(65535.9)) == 65535);
static_assert(*convertScalar<std::uint16_t>(Scalar::fromF32(65535.9f)) == 65535);
static_assert(!convertScalar<std::uint16_t>(Scalar::fromF32(65536.0f)));
static_assert(*convertScalar<float>(Scalar::fromS32(16777217)) == 16777216.0f);
static_assert(*convertScalar<std::int32_t>(Scalar::fromF64(-2147483648.75)) == INT32_MIN);
static_assert(!convertScalar<std::int32_t>(Scalar::fromF64(-2147483649.0)));
static_assert(*convertScalar<std::int16_t>(Scalar::fromF32(-32768.99f)) == INT16_MIN);
static_assert(*convertScalar<std::uint8_t>(Scalar::fromF32(-0.99f)) == 0);
static_assert(!convertScalar<std::uint8_t>(Scalar::fromF32(-1.0f)));
static_assert(!convertScalar<float>(Scalar::fromF64(3.4028235677973366e38)));  // above FLT_MAX
static_assert(*convertScalar<float>(Scalar::fromF64(3.4028234663852886e38)) == FLT_MAX);
static_assert(*convertScalar<float>(Scalar::fromU64(UINT64_MAX)) == two64f);
static_assert(!convertScalar<std::uint64_t>(Scalar::fromF32(two64f)));
static_assert(*convertScalar<float>(Scalar::fromS64(INT64_MIN)) == -two63f);
static_assert(*convertScalar<std::int64_t>(Scalar::fromF32(-two63f)) == INT64_MIN);
static_assert(!convertScalar<std::int64_t>(Scalar::fromF32(two63f)));
static_assert(!convertScalar<bool>(Scalar::fromF64(std::numeric_limits<double>::quiet_NaN())));
static_assert(*convertScalar<bool>(Scalar::fromF64(std::numeric_limits<double>::denorm_min())) == true);
static_assert(*convertScalar<bool>(Scalar::fromF64(-0.0)) == false);
static_assert(!convertScalar<std::int8_t>(Scalar::fromU8(128)));
static_assert(*convertScalar<std::int8_t>(Scalar::fromS64(-128)) == -128);
static_assert(!convertScalar<std::uint64_t>(Scalar::fromS8(-1)));
static_assert(!convertScalar<std::int64_t>(Scalar::fromU64(9223372036854775808ull)));
static_assert(*convertScalar<std::int64_t>(Scalar::fromU64(9223372036854775807ull)) == INT64_MAX);
static_assert(!convertScalar<float>(Scalar()));
static_assert(!convertScalar<bool>(Scalar()));
// U16 field limits 10..20, conversion first.
constexpr auto bounded = numericType<std::uint16_t>(15, 10, 20);
static_assert(bounded.minimum().get<std::uint16_t>() == 10);
// Defaults/limits: truncation toward zero for negative fractions.
static_assert(numericType<std::int32_t>(0, -1.5, 10).minimum().get<std::int32_t>() == -1);
static_assert(numericType<std::uint8_t>(-0.5).defaultValue().get<std::uint8_t>() == 0);
static_assert(numericType<bool>(2).defaultValue().get<bool>() == true);
static_assert(numericType<float>(16777217).defaultValue().get<float>() == 16777216.0f);
} // namespace

int main()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const float nanf = std::numeric_limits<float>::quiet_NaN();
    const float inff = std::numeric_limits<float>::infinity();

    // F64 -> integer boundaries.
    check<std::int64_t>("2^63 f64 -> s64", Scalar::fromF64(two63), std::nullopt);
    check<std::int64_t>("-2^63 f64 -> s64", Scalar::fromF64(-two63), INT64_MIN);
    check<std::int64_t>("prev(-2^63) f64 -> s64", Scalar::fromF64(std::nextafter(-two63, -inf)), std::nullopt);
    check<std::int64_t>("prev(2^63) f64 -> s64", Scalar::fromF64(std::nextafter(two63, 0.0)), 9223372036854774784LL);
    check<std::uint64_t>("2^64 f64 -> u64", Scalar::fromF64(two64), std::nullopt);
    check<std::uint64_t>("prev(2^64) f64 -> u64", Scalar::fromF64(std::nextafter(two64, 0.0)), 18446744073709549568ULL);
    check<std::uint64_t>("-0.999 f64 -> u64", Scalar::fromF64(-0.999), 0);
    check<std::uint64_t>("-1 f64 -> u64", Scalar::fromF64(-1.0), std::nullopt);
    check<std::uint32_t>("4294967295.5 f64 -> u32", Scalar::fromF64(4294967295.5), 4294967295u);
    check<std::uint32_t>("2^32 f64 -> u32", Scalar::fromF64(4294967296.0), std::nullopt);
    check<std::int32_t>("2^31 f64 -> s32", Scalar::fromF64(2147483648.0), std::nullopt);
    check<std::int32_t>("2147483647.99 f64 -> s32", Scalar::fromF64(2147483647.99), INT32_MAX);
    check<std::int32_t>("-2147483648.99 f64 -> s32", Scalar::fromF64(-2147483648.99), INT32_MIN);
    check<std::int32_t>("-2147483649 f64 -> s32", Scalar::fromF64(-2147483649.0), std::nullopt);
    check<std::uint16_t>("65535.9 f64 -> u16", Scalar::fromF64(65535.9), 65535);
    check<std::uint16_t>("65536 f64 -> u16", Scalar::fromF64(65536.0), std::nullopt);
    check<std::int8_t>("-128.9 f64 -> s8", Scalar::fromF64(-128.9), -128);
    check<std::int8_t>("-129 f64 -> s8", Scalar::fromF64(-129.0), std::nullopt);
    check<std::int8_t>("127.99 f64 -> s8", Scalar::fromF64(127.99), 127);
    // F32 -> integer boundaries.
    check<std::int32_t>("2^31 f32 -> s32", Scalar::fromF32(2147483648.0f), std::nullopt);
    check<std::int32_t>("-2^31 f32 -> s32", Scalar::fromF32(-2147483648.0f), INT32_MIN);
    check<std::int32_t>("prev(2^31) f32 -> s32", Scalar::fromF32(std::nextafter(2147483648.0f, 0.0f)), 2147483520);
    check<std::uint32_t>("2^32 f32 -> u32", Scalar::fromF32(4294967296.0f), std::nullopt);
    check<std::uint32_t>("prev(2^32) f32 -> u32", Scalar::fromF32(std::nextafter(4294967296.0f, 0.0f)), 4294967040u);
    check<std::uint16_t>("65535.9f f32 -> u16", Scalar::fromF32(65535.9f), 65535);
    check<std::int16_t>("-32768.99f f32 -> s16", Scalar::fromF32(-32768.99f), INT16_MIN);
    check<std::int16_t>("-32769f f32 -> s16", Scalar::fromF32(-32769.0f), std::nullopt);
    check<std::int64_t>("2^63 f32 -> s64", Scalar::fromF32(two63f), std::nullopt);
    check<std::int64_t>("-2^63 f32 -> s64", Scalar::fromF32(-two63f), INT64_MIN);
    check<std::uint64_t>("2^64 f32 -> u64", Scalar::fromF32(two64f), std::nullopt);
    check<std::uint64_t>("prev(2^64) f32 -> u64", Scalar::fromF32(std::nextafter(two64f, 0.0f)), 18446742974197923840ULL);
    // Special values into integers and bool.
    for (double special : {nan, inf, -inf}) {
        check<std::int64_t>("special f64 -> s64", Scalar::fromF64(special), std::nullopt);
        check<std::uint8_t>("special f64 -> u8", Scalar::fromF64(special), std::nullopt);
        check<bool>("special f64 -> bool", Scalar::fromF64(special), std::nullopt);
    }
    for (float special : {nanf, inff, -inff}) {
        check<std::uint64_t>("special f32 -> u64", Scalar::fromF32(special), std::nullopt);
        check<bool>("special f32 -> bool", Scalar::fromF32(special), std::nullopt);
    }
    check<bool>("-0.0 f64 -> bool", Scalar::fromF64(-0.0), false);
    check<bool>("denorm f32 -> bool", Scalar::fromF32(std::numeric_limits<float>::denorm_min()), true);
    check<std::int32_t>("-0.0 f32 -> s32", Scalar::fromF32(-0.0f), 0);
    check<std::uint32_t>("-denorm f64 -> u32", Scalar::fromF64(-std::numeric_limits<double>::denorm_min()), 0u);
    // Float -> float.
    check<float>("-0.0 f64 -> f32", Scalar::fromF64(-0.0), -0.0f);
    check<double>("-0.0 f32 -> f64", Scalar::fromF32(-0.0f), -0.0);
    check<float>("nan f64 -> f32", Scalar::fromF64(nan), nanf);
    check<float>("-inf f64 -> f32", Scalar::fromF64(-inf), -inff);
    check<float>("FLT_MAX f64 -> f32", Scalar::fromF64(static_cast<double>(FLT_MAX)), FLT_MAX);
    check<float>("next(FLT_MAX) f64 -> f32", Scalar::fromF64(std::nextafter(static_cast<double>(FLT_MAX), inf)), std::nullopt);
    check<float>("DBL_MAX f64 -> f32", Scalar::fromF64(DBL_MAX), std::nullopt);
    check<float>("denorm f64 -> f32", Scalar::fromF64(std::numeric_limits<double>::denorm_min()), 0.0f);
    check<float>("-denorm f64 -> f32", Scalar::fromF64(-std::numeric_limits<double>::denorm_min()), -0.0f);
    check<float>("0.1 f64 -> f32", Scalar::fromF64(0.1), 0.1f);
    // Integer -> float rounding.
    check<float>("16777217 s32 -> f32", Scalar::fromS32(16777217), 16777216.0f);
    check<float>("16777219 u32 -> f32", Scalar::fromU32(16777219u), 16777220.0f);
    check<float>("UINT64_MAX u64 -> f32", Scalar::fromU64(UINT64_MAX), two64f);
    check<double>("UINT64_MAX u64 -> f64", Scalar::fromU64(UINT64_MAX), two64);
    check<double>("2^53+1 s64 -> f64", Scalar::fromS64(9007199254740993LL), 9007199254740992.0);
    check<float>("INT64_MIN s64 -> f32", Scalar::fromS64(INT64_MIN), -two63f);
    // Round-to-nearest-even at a U64 halfway point: 2^63 + 2^39 is halfway
    // between two floats (spacing 2^40 at 2^63), so it must round to even 2^63.
    check<float>("2^63+2^39 u64 -> f32", Scalar::fromU64(9223372586610589696ULL), two63f);
    // Just above halfway rounds up.
    check<float>("2^63+2^39+1 u64 -> f32", Scalar::fromU64(9223372586610589697ULL), 9223373136366403584.0f);
    // Integer -> integer.
    check<std::int64_t>("UINT64_MAX u64 -> s64", Scalar::fromU64(UINT64_MAX), std::nullopt);
    check<std::uint32_t>("-1 s64 -> u32", Scalar::fromS64(-1), std::nullopt);
    check<std::uint32_t>("2^32 s64 -> u32", Scalar::fromS64(4294967296LL), std::nullopt);
    check<std::int32_t>("INT32_MIN s64 -> s32", Scalar::fromS64(INT32_MIN), INT32_MIN);
    check<std::int32_t>("INT32_MIN-1 s64 -> s32", Scalar::fromS64(static_cast<std::int64_t>(INT32_MIN) - 1), std::nullopt);
    check<std::uint8_t>("true -> u8", Scalar::fromBool(true), 1);
    check<std::int8_t>("true -> s8", Scalar::fromBool(true), 1);
    check<bool>("-1 s8 -> bool", Scalar::fromS8(-1), true);
    check<bool>("UINT64_MAX -> bool", Scalar::fromU64(UINT64_MAX), true);
    check<float>("true -> f32", Scalar::fromBool(true), 1.0f);
    // Null source.
    check<float>("null -> f32", Scalar(), std::nullopt);
    check<bool>("null -> bool", Scalar(), std::nullopt);

    // Write limits are inclusive and checked after conversion.
    Sink sink{Scalar::fromU16(0)};
    const Field u16{"u16", "", numericType<std::uint16_t>(15, 10, 20),
                    Getter::bind<&Sink::read>(sink), Setter::bind<&Sink::write>(sink)};
    expect(u16.write(20.99) == WriteResult::Applied && sink.value.get<std::uint16_t>() == 20, "u16 20.99 -> 20");
    expect(u16.write(9.99) == WriteResult::InvalidValue, "u16 9.99 -> 9 rejected");
    expect(u16.write(-0.0) == WriteResult::InvalidValue, "u16 -0.0 -> 0 rejected by min 10");
    const Field f32{"f32", "", numericType<float>(230, 0, 300),
                    Getter::bind<&Sink::read>(sink), Setter::bind<&Sink::write>(sink)};
    expect(f32.write(300.00001) == WriteResult::Applied && sink.value.get<float>() == 300.0f,
           "f32 300.00001 (f64) rounds to 300 and is accepted");
    expect(f32.write(300.0001) == WriteResult::InvalidValue, "f32 300.0001 rejected");
    expect(f32.write(-0.0) == WriteResult::Applied && std::signbit(sink.value.get<float>()), "f32 -0.0 accepted at min 0");
    expect(f32.write(-std::numeric_limits<double>::denorm_min()) == WriteResult::Applied
           && sink.value.get<float>() == 0.0f, "f32 -denorm f64 flushes to -0 and is accepted at min 0");
    expect(f32.write(nan) == WriteResult::InvalidValue && f32.write(inf) == WriteResult::InvalidValue,
           "restricted f32 rejects NaN/Inf");

    std::printf("%d/%d conversion edge checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
