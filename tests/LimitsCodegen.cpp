// Use the CubeIDE Cortex-M7 flags from IndexCodegen.cpp at -O2 and -Os.
// Known reads must ignore limits; full-range U16 writes need no bounds check.
// Custom checks must use native precision without an intermediate double.
#include "TelemetryIndex.h"
extern volatile float telemetry_limits_float;
extern volatile std::uint16_t telemetry_limits_integer;
namespace {
using namespace telemetry;
constexpr auto getFloat = +[]() noexcept { return telemetry_limits_float; };
constexpr auto getInteger = +[]() noexcept { return telemetry_limits_integer; };
constexpr auto setFloat = +[](const Scalar& value) noexcept {
    telemetry_limits_float = value.get<float>(); return WriteResult::Applied;
};
constexpr auto setInteger = +[](const Scalar& value) noexcept {
    telemetry_limits_integer = value.get<std::uint16_t>(); return WriteResult::Applied;
};
constexpr Field rows[] = {
    {0, "f32", "", numericType<float>(-10, 300, 0), getFloat, setFloat},
    {1, "u16", "", numericType<std::uint16_t>(10, 20, 15), getInteger, setInteger},
    {2, "full", "", ScalarType::U16, getInteger, setInteger},
};
constexpr Catalog catalogs[] = {{0, "v", rows}};
constexpr auto index = CatalogIndex::bind<catalogs>();
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(FieldType) == 40 && sizeof(Field) == 80);
#endif
}
extern "C" {
__attribute__((noinline)) float limits_read_float() noexcept { return index.read<0>().value_or(0); }
__attribute__((noinline)) std::uint16_t limits_read_integer() noexcept { return index.read<1>().value_or(0); }
__attribute__((noinline)) telemetry::WriteResult limits_write_float(float value) noexcept { return index.write(0, value); }
__attribute__((noinline)) telemetry::WriteResult limits_write_integer(std::uint16_t value) noexcept { return index.write(1, value); }
__attribute__((noinline)) telemetry::WriteResult limits_write_full(std::uint16_t value) noexcept { return index.write(2, value); }
__attribute__((noinline)) telemetry::WriteResult limits_write_rejected() noexcept { return index.write(1, 21); }
}
