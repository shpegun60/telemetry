// Review probe (numeric-core): Clang's -fno-honor-nans / -fno-honor-infinities
// each leave __FINITE_MATH_ONLY__ at 0, so the header guard does not fire.
// This checks whether the conversion's NaN/Inf branches still behave.
// clang++-18 -std=c++17 -O2 -fno-honor-infinities -Ilib/telemetry -Ilib/delegate ...
#include "field/TelemetryField.h"

#include <cstdio>
#include <cstring>

using namespace telemetry;

namespace {
Scalar stored;
int writes = 0;
WriteResult store(const Scalar& value) noexcept { ++writes; stored = value; return WriteResult::Applied; }

template <class T, class Bits>
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

__attribute__((noinline)) bool finiteF32(float value) noexcept { return detail::scalarFinite(value); }
__attribute__((noinline)) bool f64ToF32(double value, float& out) noexcept { return detail::convertNumberTo(value, out); }
__attribute__((noinline)) bool f32ToS32(float value, std::int32_t& out) noexcept { return detail::convertNumberTo(value, out); }
__attribute__((noinline)) WriteResult writeF32(const Field& field, float value) noexcept { return field.write(value); }
} // namespace

int main()
{
    const float inff = opaque<float>(0x7f800000u);
    const float nanf = opaque<float>(0x7fc00000u);
    const double inf = opaque<double>(0x7ff0000000000000ull);
    const double nan = opaque<double>(0x7ff8000000000000ull);
    int failures = 0;
    auto report = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL  %s\n", what); } };

    report(!finiteF32(inff), "scalarFinite(+inf f32) is false");
    report(!finiteF32(nanf), "scalarFinite(nan f32) is false");
    float out = 0;
    report(f64ToF32(inf, out) && bitsOf(out) == 0x7f800000u, "+inf f64 -> f32 preserved (bit check)");
    report(f64ToF32(nan, out) && (bitsOf(out) & 0x7fffffffu) > 0x7f800000u, "nan f64 -> f32 preserved (bit check)");
    std::int32_t integer = 7;
    report(!f32ToS32(nanf, integer) && integer == 7, "nan f32 -> s32 rejected");
    report(!f32ToS32(inff, integer) && integer == 7, "+inf f32 -> s32 rejected");

    const Field plain{"plain", "", ScalarType::F32, nullptr, &store};
    report(writeF32(plain, inff) == WriteResult::InvalidValue && writes == 0, "unrestricted F32 rejects +inf write");
    report(writeF32(plain, nanf) == WriteResult::InvalidValue && writes == 0, "unrestricted F32 rejects nan write");
    std::printf("%s  honor-flags probe (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures == 0 ? 0 : 1;
}
