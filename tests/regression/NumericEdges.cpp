// Regression promoted from tests/review/numeric-core/ConversionEdges.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Hand-derived checked native number boundaries, constexpr and runtime (MIT).
// Checks hand-derived numeric boundaries, special IEEE values and output preservation on refusal.
// The cases also cover constants and aliased runtime outputs where conversion order matters.

#include <telemetry/detail/NumberConversion.hpp>
#include <optional>

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
	if (!ok) {
		++failures;
		std::printf("FAIL  %s\n", what);
	}
}

template<class T>
bool same(T a, T b)
{
	if constexpr (std::is_floating_point_v<T>)
		return (std::isnan(a) && std::isnan(b)) || (a == b && std::signbit(a) == std::signbit(b));
	else
		return a == b;
}

template<class To, class From>
constexpr std::optional<To> converted(From input) noexcept
{
	To output{};
	return detail::convertNumberTo(input, output) ? std::optional<To>{output} : std::nullopt;
}

template<class To, class From>
void check(const char* label, From input, std::optional<To> expected)
{
	To output = static_cast<To>(7);
	const auto before = output;
	const bool ok = detail::convertNumberTo(input, output);
	expect(ok == expected.has_value() && (ok ? same(output, *expected) : same(output, before)),
	       label);
}

constexpr double two63 = 9223372036854775808.0;
constexpr double two64 = 18446744073709551616.0;
constexpr float two63f = 9223372036854775808.0f;
constexpr float two64f = 18446744073709551616.0f;

// Compile-time evaluation of the same primitives (constexpr/runtime parity).
static_assert(!converted<std::int64_t>(static_cast<double>(two63)));
static_assert(*converted<std::int64_t>(static_cast<double>(-two63)) == INT64_MIN);
static_assert(!converted<std::uint64_t>(static_cast<double>(two64)));
static_assert(*converted<std::uint64_t>(static_cast<double>(two64 - 4096.0)) ==
              18446744073709547520ull);
static_assert(*converted<std::uint32_t>(static_cast<double>(4294967295.5)) == 4294967295u);
static_assert(!converted<std::uint32_t>(static_cast<double>(4294967296.0)));
static_assert(*converted<std::uint16_t>(static_cast<double>(65535.9)) == 65535);
static_assert(*converted<std::uint16_t>(static_cast<float>(65535.9f)) == 65535);
static_assert(!converted<std::uint16_t>(static_cast<float>(65536.0f)));
static_assert(*converted<float>(static_cast<std::int32_t>(16777217)) == 16777216.0f);
static_assert(*converted<std::int32_t>(static_cast<double>(-2147483648.75)) == INT32_MIN);
static_assert(!converted<std::int32_t>(static_cast<double>(-2147483649.0)));
static_assert(*converted<std::int16_t>(static_cast<float>(-32768.99f)) == INT16_MIN);
static_assert(*converted<std::uint8_t>(static_cast<float>(-0.99f)) == 0);
static_assert(!converted<std::uint8_t>(static_cast<float>(-1.0f)));
static_assert(!converted<float>(static_cast<double>(3.4028235677973366e38))); // above FLT_MAX
static_assert(*converted<float>(static_cast<double>(3.4028234663852886e38)) == FLT_MAX);
static_assert(*converted<float>(static_cast<std::uint64_t>(UINT64_MAX)) == two64f);
static_assert(!converted<std::uint64_t>(static_cast<float>(two64f)));
static_assert(*converted<float>(static_cast<std::int64_t>(INT64_MIN)) == -two63f);
static_assert(*converted<std::int64_t>(static_cast<float>(-two63f)) == INT64_MIN);
static_assert(!converted<std::int64_t>(static_cast<float>(two63f)));
static_assert(!converted<bool>(static_cast<double>(std::numeric_limits<double>::quiet_NaN())));
static_assert(*converted<bool>(static_cast<double>(std::numeric_limits<double>::denorm_min())) ==
              true);
static_assert(*converted<bool>(static_cast<double>(-0.0)) == false);
static_assert(!converted<std::int8_t>(static_cast<std::uint8_t>(128)));
static_assert(*converted<std::int8_t>(static_cast<std::int64_t>(-128)) == -128);
static_assert(!converted<std::uint64_t>(static_cast<std::int8_t>(-1)));
static_assert(!converted<std::int64_t>(static_cast<std::uint64_t>(9223372036854775808ull)));
static_assert(*converted<std::int64_t>(static_cast<std::uint64_t>(9223372036854775807ull)) ==
              INT64_MAX);
} // namespace

int main()
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	const float nanf = std::numeric_limits<float>::quiet_NaN();
	const float inff = std::numeric_limits<float>::infinity();

	// F64 -> integer boundaries.
	check<std::int64_t>("2^63 f64 -> s64", static_cast<double>(two63), std::nullopt);
	check<std::int64_t>("-2^63 f64 -> s64", static_cast<double>(-two63), INT64_MIN);
	check<std::int64_t>("prev(-2^63) f64 -> s64", static_cast<double>(std::nextafter(-two63, -inf)),
	                    std::nullopt);
	check<std::int64_t>("prev(2^63) f64 -> s64", static_cast<double>(std::nextafter(two63, 0.0)),
	                    9223372036854774784LL);
	check<std::uint64_t>("2^64 f64 -> u64", static_cast<double>(two64), std::nullopt);
	check<std::uint64_t>("prev(2^64) f64 -> u64", static_cast<double>(std::nextafter(two64, 0.0)),
	                     18446744073709549568ULL);
	check<std::uint64_t>("-0.999 f64 -> u64", static_cast<double>(-0.999), 0);
	check<std::uint64_t>("-1 f64 -> u64", static_cast<double>(-1.0), std::nullopt);
	check<std::uint32_t>("4294967295.5 f64 -> u32", static_cast<double>(4294967295.5), 4294967295u);
	check<std::uint32_t>("2^32 f64 -> u32", static_cast<double>(4294967296.0), std::nullopt);
	check<std::int32_t>("2^31 f64 -> s32", static_cast<double>(2147483648.0), std::nullopt);
	check<std::int32_t>("2147483647.99 f64 -> s32", static_cast<double>(2147483647.99), INT32_MAX);
	check<std::int32_t>("-2147483648.99 f64 -> s32", static_cast<double>(-2147483648.99),
	                    INT32_MIN);
	check<std::int32_t>("-2147483649 f64 -> s32", static_cast<double>(-2147483649.0), std::nullopt);
	check<std::uint16_t>("65535.9 f64 -> u16", static_cast<double>(65535.9), 65535);
	check<std::uint16_t>("65536 f64 -> u16", static_cast<double>(65536.0), std::nullopt);
	check<std::int8_t>("-128.9 f64 -> s8", static_cast<double>(-128.9), -128);
	check<std::int8_t>("-129 f64 -> s8", static_cast<double>(-129.0), std::nullopt);
	check<std::int8_t>("127.99 f64 -> s8", static_cast<double>(127.99), 127);
	// F32 -> integer boundaries.
	check<std::int32_t>("2^31 f32 -> s32", static_cast<float>(2147483648.0f), std::nullopt);
	check<std::int32_t>("-2^31 f32 -> s32", static_cast<float>(-2147483648.0f), INT32_MIN);
	check<std::int32_t>("prev(2^31) f32 -> s32",
	                    static_cast<float>(std::nextafter(2147483648.0f, 0.0f)), 2147483520);
	check<std::uint32_t>("2^32 f32 -> u32", static_cast<float>(4294967296.0f), std::nullopt);
	check<std::uint32_t>("prev(2^32) f32 -> u32",
	                     static_cast<float>(std::nextafter(4294967296.0f, 0.0f)), 4294967040u);
	check<std::uint16_t>("65535.9f f32 -> u16", static_cast<float>(65535.9f), 65535);
	check<std::int16_t>("-32768.99f f32 -> s16", static_cast<float>(-32768.99f), INT16_MIN);
	check<std::int16_t>("-32769f f32 -> s16", static_cast<float>(-32769.0f), std::nullopt);
	check<std::int64_t>("2^63 f32 -> s64", static_cast<float>(two63f), std::nullopt);
	check<std::int64_t>("-2^63 f32 -> s64", static_cast<float>(-two63f), INT64_MIN);
	check<std::uint64_t>("2^64 f32 -> u64", static_cast<float>(two64f), std::nullopt);
	check<std::uint64_t>("prev(2^64) f32 -> u64", static_cast<float>(std::nextafter(two64f, 0.0f)),
	                     18446742974197923840ULL);
	// Special values into integers and bool.
	for (double special : {nan, inf, -inf}) {
		check<std::int64_t>("special f64 -> s64", static_cast<double>(special), std::nullopt);
		check<std::uint8_t>("special f64 -> u8", static_cast<double>(special), std::nullopt);
		check<bool>("special f64 -> bool", static_cast<double>(special), std::nullopt);
	}
	for (float special : {nanf, inff, -inff}) {
		check<std::uint64_t>("special f32 -> u64", static_cast<float>(special), std::nullopt);
		check<bool>("special f32 -> bool", static_cast<float>(special), std::nullopt);
	}
	check<bool>("-0.0 f64 -> bool", static_cast<double>(-0.0), false);
	check<bool>("denorm f32 -> bool", static_cast<float>(std::numeric_limits<float>::denorm_min()),
	            true);
	check<std::int32_t>("-0.0 f32 -> s32", static_cast<float>(-0.0f), 0);
	check<std::uint32_t>("-denorm f64 -> u32",
	                     static_cast<double>(-std::numeric_limits<double>::denorm_min()), 0u);
	// Float -> float.
	check<float>("-0.0 f64 -> f32", static_cast<double>(-0.0), -0.0f);
	check<double>("-0.0 f32 -> f64", static_cast<float>(-0.0f), -0.0);
	check<float>("nan f64 -> f32", static_cast<double>(nan), nanf);
	check<float>("-inf f64 -> f32", static_cast<double>(-inf), -inff);
	check<float>("FLT_MAX f64 -> f32", static_cast<double>(static_cast<double>(FLT_MAX)), FLT_MAX);
	check<float>("next(FLT_MAX) f64 -> f32",
	             static_cast<double>(std::nextafter(static_cast<double>(FLT_MAX), inf)),
	             std::nullopt);
	check<float>("DBL_MAX f64 -> f32", static_cast<double>(DBL_MAX), std::nullopt);
	check<float>("denorm f64 -> f32",
	             static_cast<double>(std::numeric_limits<double>::denorm_min()), 0.0f);
	check<float>("-denorm f64 -> f32",
	             static_cast<double>(-std::numeric_limits<double>::denorm_min()), -0.0f);
	check<float>("0.1 f64 -> f32", static_cast<double>(0.1), 0.1f);
	// Integer -> float rounding.
	check<float>("16777217 s32 -> f32", static_cast<std::int32_t>(16777217), 16777216.0f);
	check<float>("16777219 u32 -> f32", static_cast<std::uint32_t>(16777219u), 16777220.0f);
	check<float>("UINT64_MAX u64 -> f32", static_cast<std::uint64_t>(UINT64_MAX), two64f);
	check<double>("UINT64_MAX u64 -> f64", static_cast<std::uint64_t>(UINT64_MAX), two64);
	check<double>("2^53+1 s64 -> f64", static_cast<std::int64_t>(9007199254740993LL),
	              9007199254740992.0);
	check<float>("INT64_MIN s64 -> f32", static_cast<std::int64_t>(INT64_MIN), -two63f);
	// Round-to-nearest-even at a U64 halfway point: 2^63 + 2^39 is halfway
	// between two floats (spacing 2^40 at 2^63), so it must round to even 2^63.
	check<float>("2^63+2^39 u64 -> f32", static_cast<std::uint64_t>(9223372586610589696ULL),
	             two63f);
	// Just above halfway rounds up.
	check<float>("2^63+2^39+1 u64 -> f32", static_cast<std::uint64_t>(9223372586610589697ULL),
	             9223373136366403584.0f);
	// Integer -> integer.
	check<std::int64_t>("UINT64_MAX u64 -> s64", static_cast<std::uint64_t>(UINT64_MAX),
	                    std::nullopt);
	check<std::uint32_t>("-1 s64 -> u32", static_cast<std::int64_t>(-1), std::nullopt);
	check<std::uint32_t>("2^32 s64 -> u32", static_cast<std::int64_t>(4294967296LL), std::nullopt);
	check<std::int32_t>("INT32_MIN s64 -> s32", static_cast<std::int64_t>(INT32_MIN), INT32_MIN);
	check<std::int32_t>("INT32_MIN-1 s64 -> s32",
	                    static_cast<std::int64_t>(static_cast<std::int64_t>(INT32_MIN) - 1),
	                    std::nullopt);
	check<std::uint8_t>("true -> u8", static_cast<bool>(true), 1);
	check<std::int8_t>("true -> s8", static_cast<bool>(true), 1);
	check<bool>("-1 s8 -> bool", static_cast<std::int8_t>(-1), true);
	check<bool>("UINT64_MAX -> bool", static_cast<std::uint64_t>(UINT64_MAX), true);
	check<float>("true -> f32", static_cast<bool>(true), 1.0f);

	std::printf("CHECKS %d\n", checks);
	return failures == 0 ? 0 : 1;
}
