// Review probe (numeric-core): does the fast-math guard in
// detail/TelemetryNumberConversion.h reject each unsafe floating mode, and if a
// mode slips through, do the NaN/Inf checks still work at runtime?
// Compile with the floating option under test; a guard hit is a compile error.
#include "field/TelemetryField.h"

#include <cstdio>
#include <cstring>
#include <limits>

using namespace telemetry;

namespace {
Scalar stored;
WriteResult store(const Scalar& value) noexcept { stored = value; return WriteResult::Applied; }

// Keep inputs opaque so the optimizer cannot fold them.
__attribute__((noinline)) double opaqueDouble(unsigned long long bits) noexcept
{
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
} // namespace

int main()
{
    const double nan = opaqueDouble(0x7ff8000000000000ULL);
    const double inf = opaqueDouble(0x7ff0000000000000ULL);
    int failures = 0;

    const auto asInt = convertScalar<std::int32_t>(Scalar::fromF64(nan));
    if (asInt) { std::printf("FAIL  NaN f64 -> s32 accepted (%ld)\n", static_cast<long>(*asInt)); ++failures; }
    const auto asU64 = convertScalar<std::uint64_t>(Scalar::fromF64(inf));
    if (asU64) { std::printf("FAIL  Inf f64 -> u64 accepted\n"); ++failures; }
    const auto asBool = convertScalar<bool>(Scalar::fromF64(nan));
    if (asBool) { std::printf("FAIL  NaN f64 -> bool accepted\n"); ++failures; }
    const auto asFloat = convertScalar<float>(Scalar::fromF64(nan));
    if (!asFloat) { std::printf("FAIL  NaN f64 -> f32 rejected\n"); ++failures; }

    const Field plain{"plain", "", ScalarType::F32, nullptr, &store};
    if (plain.write(nan) != WriteResult::InvalidValue) { std::printf("FAIL  NaN write accepted by unrestricted F32\n"); ++failures; }
    if (plain.write(inf) != WriteResult::InvalidValue) { std::printf("FAIL  Inf write accepted by unrestricted F32\n"); ++failures; }
    const Field bounded{"bounded", "", numericType<double>(0, -1, 1), nullptr, &store};
    if (bounded.write(nan) != WriteResult::InvalidValue) { std::printf("FAIL  NaN write accepted by restricted F64\n"); ++failures; }

    std::printf("%s  fast-math probe (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures == 0 ? 0 : 1;
}
