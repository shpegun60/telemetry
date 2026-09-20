// Compile-only rejection probes. Each case must fail under C++17 and C++20:
// From the telemetry repository root:
// g++ -std=c++17 -Ilib/telemetry -Ilib/delegate
//     -DTELEMETRY_READ_FAIL_CASE=N -fsyntax-only tests/TelemetryReadCompileFail.cpp
#include "catalog/TelemetryIndex.h"
#include "field/TelemetryEnum.h"
using namespace telemetry;

constexpr Field fields[] = {
    {"Ua", "V", ScalarType::F32, []() noexcept { return 230.0f; }},
    {"NullType", "", ScalarType::Null},
    {"PastGap", "V", ScalarType::F32},
};
constexpr Catalog catalogs[] = {{"meter", fields}, {"PastGroupGap", fields}};
constexpr auto index = CatalogIndex::bind<catalogs>();

#if TELEMETRY_READ_FAIL_CASE == 1
auto rejected = index.read<makeId(2, 0)>(); // Missing group.
#elif TELEMETRY_READ_FAIL_CASE == 2
auto rejected = index.read<makeId(0, 3)>(); // Past the accepted field prefix.
#elif TELEMETRY_READ_FAIL_CASE == 3
auto rejected = index.read<makeId(0, 1)>(); // Null has no native numeric type.
#elif TELEMETRY_READ_FAIL_CASE == 4
auto rejected = index.read<long double>(makeId(0, 0)); // Unsupported numeric type.
#elif TELEMETRY_READ_FAIL_CASE == 5
Catalog runtimeCatalogs[] = {{"meter", fields}};
auto rejected = CatalogIndex::bind<runtimeCatalogs>(); // Metadata is not constexpr.
#elif TELEMETRY_READ_FAIL_CASE == 6
void rejected(Scalar& value) { value.type() = ScalarType::U64; } // Tag is not writable.
#elif TELEMETRY_READ_FAIL_CASE == 7
auto rejected = Scalar::fromF32(1).getIf<float>(); // Borrowing from a temporary.
#elif TELEMETRY_READ_FAIL_CASE == 8
float throwingRead() { return 1; }
auto rejected = Getter::bind<&throwingRead>();
#elif TELEMETRY_READ_FAIL_CASE == 9
WriteResult throwingWrite(const Scalar&) { return WriteResult::Applied; }
auto rejected = Setter::bind<&throwingWrite>();
#elif TELEMETRY_READ_FAIL_CASE == 10
struct Owner { float read() const noexcept { return 1; } };
auto rejected = Getter::bind<&Owner::read>(Owner{});
#elif TELEMETRY_READ_FAIL_CASE == 11
constexpr auto noFunction = static_cast<float (*)() noexcept>(nullptr);
auto rejected = Getter::bind<noFunction>();
#elif TELEMETRY_READ_FAIL_CASE == 12
using Rows = Field[1];
auto rejected = Catalog{"temporary", Rows{}, 1};
#elif TELEMETRY_READ_FAIL_CASE == 13
using Groups = Catalog[1];
auto rejected = CatalogIndex{Groups{}, 1};
#elif TELEMETRY_READ_FAIL_CASE == 14
auto rejected = enumType<int>();
#elif TELEMETRY_READ_FAIL_CASE == 15
enum class Mode { Off, Auto };
auto rejected = enumType<Mode, static_cast<Mode>(99)>();
#elif TELEMETRY_READ_FAIL_CASE == 16
enum class Mode { Off, Alias = Off };
auto rejected = enumType<Mode, Mode::Off, Mode::Alias>();
#elif TELEMETRY_READ_FAIL_CASE == 17
enum class Mode { OutsideDefaultScan = 100000 };
auto rejected = enumType<Mode>();
#elif TELEMETRY_READ_FAIL_CASE == 18
enum class Mode : std::uint8_t { Off, Auto };
auto rejected = index.read<Mode>(0); // Applications cast the numeric result.
#elif TELEMETRY_READ_FAIL_CASE == 19
enum class Mode : std::uint8_t { Off, Auto };
auto rejected = index.write(0, Mode::Auto); // Applications supply a number.
#elif TELEMETRY_READ_FAIL_CASE == 20
constexpr auto rejected = numericType<int>(15, 20, 10); // Reversed interval.
#elif TELEMETRY_READ_FAIL_CASE == 21
constexpr auto rejected = numericType<float>(11, 0, 10); // Default outside interval.
#elif TELEMETRY_READ_FAIL_CASE == 22
constexpr auto rejected = numericType<std::uint8_t>(0, 0, 256); // Bound not representable.
#elif TELEMETRY_READ_FAIL_CASE == 23
constexpr auto rejected = numericType<float>(std::numeric_limits<float>::quiet_NaN(), -1, 1);
#elif TELEMETRY_READ_FAIL_CASE == 24
constexpr auto rejected = numericType<double>(0, -1, std::numeric_limits<double>::infinity());
#elif TELEMETRY_READ_FAIL_CASE == 25
constexpr auto rejected = numericType<int>(0, Scalar::null(), 10);
#elif TELEMETRY_READ_FAIL_CASE == 26
constexpr auto rejected = FieldType{}.withLimits(0, 1, 0); // Null cannot have numeric limits.
#elif TELEMETRY_READ_FAIL_CASE == 27
enum class Mode : std::uint8_t { Off, Auto };
constexpr auto rejected = enumType<Mode>().withDefault(2);
#elif TELEMETRY_READ_FAIL_CASE == 28
constexpr auto rejected = numericType<std::uint8_t>(256); // Check before narrowing to U8.
#elif TELEMETRY_READ_FAIL_CASE == 29
constexpr auto rejected = numericType<float>(std::numeric_limits<float>::quiet_NaN());
#elif TELEMETRY_READ_FAIL_CASE == 30
constexpr auto rejected = numericType<int>(5, 10); // Chosen minimum above default.
#elif TELEMETRY_READ_FAIL_CASE == 31
struct Owner { float read() const noexcept { return 1; } };
auto rejected = Getter::bind<&Owner::read, const Owner>(Owner{});
#elif TELEMETRY_READ_FAIL_CASE == 32
struct Owner { float read() const noexcept { return 1; } };
auto rejected = Getter::bind<&Owner::read, const Owner&>(Owner{});
#elif TELEMETRY_READ_FAIL_CASE == 33
struct Owner { WriteResult write(const Scalar&) const noexcept { return WriteResult::Applied; } };
auto rejected = Setter::bind<&Owner::write, const Owner>(Owner{});
#elif TELEMETRY_READ_FAIL_CASE == 34
struct Owner { WriteResult write(const Scalar&) const noexcept { return WriteResult::Applied; } };
auto rejected = Setter::bind<&Owner::write, const Owner&>(Owner{});
#elif TELEMETRY_READ_FAIL_CASE == 35
struct Owner { WriteResult write(const Scalar&) const noexcept { return WriteResult::Applied; } };
auto rejected = Setter::bind<&Owner::write>(Owner{});
#elif TELEMETRY_READ_FAIL_CASE == 36
auto rejected = index.read<makeId(2, 0), float>(); // Missing compile-time group.
#elif TELEMETRY_READ_FAIL_CASE == 37
auto rejected = index.write<makeId(0, 3)>(1.0f); // Past the accepted field prefix.
#elif TELEMETRY_READ_FAIL_CASE == 38
auto rejected = index.read<makeId(0, 0), long double>(); // Unsupported requested type.
#elif TELEMETRY_READ_FAIL_CASE == 39
enum class Mode : std::uint8_t { Off, Auto };
auto rejected = index.write<makeId(0, 0)>(Mode::Auto); // Applications supply a number.
#else
#error "Select TELEMETRY_READ_FAIL_CASE from 1 through 39"
#endif
