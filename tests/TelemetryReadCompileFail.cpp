// Compile-only rejection probes. Each case must fail under C++17 and C++20:
// From the telemetry repository root:
// g++ -std=c++17 -Ilib/telemetry -Ilib/delegate
//     -DTELEMETRY_READ_FAIL_CASE=N -fsyntax-only tests/TelemetryReadCompileFail.cpp
#include "TelemetryIndex.h"
using namespace telemetry;

constexpr Field fields[] = {
    {makeId(0, 0), "Ua", "V", ScalarType::F32, []() noexcept { return 230.0f; }},
    {makeId(0, 1), "NullType", "", ScalarType::Null},
    {makeId(0, 3), "PastGap", "V", ScalarType::F32},
};
constexpr Catalog catalogs[] = {{0, "meter", fields}, {2, "PastGroupGap", fields}};
constexpr auto index = CatalogIndex::bind<catalogs>();

#if TELEMETRY_READ_FAIL_CASE == 1
auto rejected = index.read<makeId(1, 0)>(); // Missing group.
#elif TELEMETRY_READ_FAIL_CASE == 2
auto rejected = index.read<makeId(0, 3)>(); // Past the accepted field prefix.
#elif TELEMETRY_READ_FAIL_CASE == 3
auto rejected = index.read<makeId(0, 1)>(); // Null has no native numeric type.
#elif TELEMETRY_READ_FAIL_CASE == 4
auto rejected = index.read<long double>(makeId(0, 0)); // Unsupported numeric type.
#elif TELEMETRY_READ_FAIL_CASE == 5
Catalog runtimeCatalogs[] = {{0, "meter", fields}};
auto rejected = CatalogIndex::bind<runtimeCatalogs>(); // Metadata is not constexpr.
#elif TELEMETRY_READ_FAIL_CASE == 6
void rejected(Scalar& value) { value.type() = ScalarType::U64; } // Tag is not writable.
#elif TELEMETRY_READ_FAIL_CASE == 7
auto rejected = Scalar::fromF32(1).getIf<float>(); // Borrowing from a temporary.
#else
#error "Select TELEMETRY_READ_FAIL_CASE from 1 through 7"
#endif
