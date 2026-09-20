// Compile with the Cortex-M7 flags documented in IndexCodegen.cpp, at -O2/-Os.
// Each known enum/plain pair must produce the same numeric instructions.
// Also inspect dynamic Field accesses: no schema callback may be loaded/called.
#include "field/TelemetryEnum.h"
#include "catalog/TelemetryIndex.h"

extern volatile std::uint16_t telemetry_enum_source;
extern volatile std::uint16_t telemetry_enum_sink;

namespace {
using namespace telemetry;
enum class Mode : std::uint16_t { Off, Auto, Manual };
constexpr auto get = +[]() noexcept { return telemetry_enum_source; };
constexpr auto set = +[](const Scalar& value) noexcept {
    telemetry_enum_sink = value.get<std::uint16_t>();
    return WriteResult::Applied;
};
constexpr Field plain[] = {{"Mode", "", numericType<std::uint16_t>(0, 0, 2), get, set}};
constexpr Field described[] = {{"Mode", "", enumType<Mode>(), get, set}};
constexpr Catalog plainCatalogs[] = {{"v", plain}};
constexpr Catalog enumCatalogs[] = {{"v", described}};
constexpr auto plainIndex = CatalogIndex::bind<plainCatalogs>();
constexpr auto enumIndex = CatalogIndex::bind<enumCatalogs>();
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(FieldType) == 48 && sizeof(Field) == 96 && sizeof(Scalar) == 16);
static_assert(alignof(Field) == 32);
#endif
}

extern "C" {
__attribute__((noinline)) std::uint16_t enum_read_plain() noexcept { return plainIndex.read<0>().value_or(0); }
__attribute__((noinline)) std::uint16_t enum_read_described() noexcept { return enumIndex.read<0>().value_or(0); }
__attribute__((noinline)) telemetry::WriteResult enum_write_plain(std::uint16_t value) noexcept { return plainIndex.write(0, value); }
__attribute__((noinline)) telemetry::WriteResult enum_write_described(std::uint16_t value) noexcept { return enumIndex.write(0, value); }
__attribute__((noinline)) telemetry::Scalar enum_scalar_plain() noexcept { return plainIndex.read(0); }
__attribute__((noinline)) telemetry::Scalar enum_scalar_described() noexcept { return enumIndex.read(0); }
__attribute__((noinline)) std::uint16_t enum_dynamic_read(const telemetry::Field& field) noexcept { return field.read<std::uint16_t>().value_or(0); }
__attribute__((noinline)) telemetry::WriteResult enum_dynamic_write(const telemetry::Field& field, std::uint16_t value) noexcept { return field.write(value); }
__attribute__((noinline)) const telemetry::Field* enum_plain_table() noexcept { return plain; }
__attribute__((noinline)) const telemetry::Field* enum_described_table() noexcept { return described; }
}
