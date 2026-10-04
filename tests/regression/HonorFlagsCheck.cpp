// Regression promoted from tests/review/numeric-core/HonorFlagsProbe.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Checks NaN and infinity handling through opaque IEEE bit patterns in ordinary supported builds.
// The runner separately checks rejection of compiler modes that discard these required representations.

#include <telemetry/detail/NumberConversion.hpp>

#include <cstdio>
#include <cstring>

using namespace telemetry;

namespace {
template<class T, class Bits>
__attribute__((noinline)) T opaque(Bits bits) noexcept
{
	T value;
	std::memcpy(&value, &bits, sizeof value);
	return value;
}

std::uint32_t bitsOf(float value) noexcept
{
	std::uint32_t bits;
	std::memcpy(&bits, &value, sizeof bits);
	return bits;
}

__attribute__((noinline)) bool finiteF32(float value) noexcept
{
	return detail::scalarFinite(value);
}

__attribute__((noinline)) bool f64ToF32(double value, float& out) noexcept
{
	return detail::convertNumberTo(value, out);
}

__attribute__((noinline)) bool f32ToS32(float value, std::int32_t& out) noexcept
{
	return detail::convertNumberTo(value, out);
}
} // namespace

int main()
{
	const float inff = opaque<float>(0x7f800000u);
	const float nanf = opaque<float>(0x7fc00000u);
	const double inf = opaque<double>(0x7ff0000000000000ull);
	const double nan = opaque<double>(0x7ff8000000000000ull);
	int failures = 0, checks = 0;
	auto report = [&](bool ok, const char* what) {
		++checks;
		if (!ok) {
			++failures;
			std::printf("FAIL  %s\n", what);
		}
	};

	report(!finiteF32(inff), "scalarFinite(+inf f32) is false");
	report(!finiteF32(nanf), "scalarFinite(nan f32) is false");
	float out = 0;
	report(f64ToF32(inf, out) && bitsOf(out) == 0x7f800000u,
	       "+inf f64 -> f32 preserved (bit check)");
	report(f64ToF32(nan, out) && (bitsOf(out) & 0x7fffffffu) > 0x7f800000u,
	       "nan f64 -> f32 preserved (bit check)");
	std::int32_t integer = 7;
	report(!f32ToS32(nanf, integer) && integer == 7, "nan f32 -> s32 rejected");
	report(!f32ToS32(inff, integer) && integer == 7, "+inf f32 -> s32 rejected");

	std::printf("CHECKS %d\n", checks);
	return failures == 0 ? 0 : 1;
}
