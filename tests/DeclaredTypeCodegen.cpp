// Compile-only probes for getter -> declaredType -> requested type, and writes.
// Use IndexCodegen.cpp's Cortex-M7 flags at -O2/-Os. Runtime sources keep the
// values dynamic while their types, catalog metadata and IDs remain constant.
#include "catalog/TelemetryIndex.h"

extern volatile float telemetry_declared_f32;
extern volatile double telemetry_declared_f64;
extern volatile std::uint16_t telemetry_declared_u16;
extern volatile std::uint64_t telemetry_declared_u64;
extern volatile float telemetry_declared_sink_f32;
extern volatile std::uint16_t telemetry_declared_sink_u16;

namespace {
using namespace telemetry;
constexpr Field fields[] = {
    {makeId(0, 0), "same", "", ScalarType::F32, []() noexcept { return telemetry_declared_f32; },
     [](const Scalar& value) noexcept {
         telemetry_declared_sink_f32 = value.get<float>();
         return WriteResult::Applied;
     }},
    {makeId(0, 1), "widen", "", ScalarType::F64, []() noexcept { return telemetry_declared_f32; }},
    {makeId(0, 2), "narrow", "", ScalarType::F32, []() noexcept { return telemetry_declared_f64; }},
    {makeId(0, 3), "truncate", "", ScalarType::U16, []() noexcept { return telemetry_declared_f32; },
     [](const Scalar& value) noexcept {
         telemetry_declared_sink_u16 = value.get<std::uint16_t>();
         return WriteResult::Applied;
     }},
    {makeId(0, 4), "bool", "", ScalarType::Bool, []() noexcept { return telemetry_declared_f32; }},
    {makeId(0, 5), "integer", "", ScalarType::U32, []() noexcept { return telemetry_declared_u16; }},
    {makeId(0, 6), "exact", "", ScalarType::U64, []() noexcept { return telemetry_declared_u64; }},
};
constexpr Catalog catalogs[] = {{0, "conversion", fields}};
constexpr auto index = CatalogIndex::bind<catalogs>();
}

extern "C" {
__attribute__((noinline)) float declared_read_same() noexcept { return index.read<0>().value_or(0); }
__attribute__((noinline)) double declared_read_f32_f64() noexcept { return index.read<1>().value_or(0); }
__attribute__((noinline)) float declared_read_f64_f32() noexcept { return index.read<2>().value_or(0); }
__attribute__((noinline)) std::uint16_t declared_read_f32_u16() noexcept { return index.read<3>().value_or(0); }
__attribute__((noinline)) bool declared_read_f32_bool() noexcept { return index.read<4>().value_or(false); }
__attribute__((noinline)) std::uint32_t declared_read_u16_u32() noexcept { return index.read<5>().value_or(0); }
__attribute__((noinline)) std::uint64_t declared_read_u64_same() noexcept { return index.read<6>().value_or(0); }
__attribute__((noinline)) double declared_read_f64_via_f32() noexcept { return index.read<double>(2).value_or(0); }
__attribute__((noinline)) float declared_read_f32_via_u16() noexcept { return index.read<float>(3).value_or(0); }
__attribute__((noinline)) telemetry::WriteResult declared_write_same(float value) noexcept { return index.write(0, value); }
__attribute__((noinline)) telemetry::WriteResult declared_write_f64_f32(double value) noexcept { return index.write(0, value); }
__attribute__((noinline)) telemetry::WriteResult declared_write_f32_u16(float value) noexcept { return index.write(3, value); }
__attribute__((noinline)) telemetry::WriteResult declared_write_u16_same(std::uint16_t value) noexcept { return index.write(3, value); }
}
