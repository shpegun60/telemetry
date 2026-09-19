#include "Fixture.h"

// 1024 fields make the table footprint visible. Repeated names are deliberate
// size/codegen fixtures; only the first eight unique rows are used for JSON.
#ifndef LAYOUT_FIELD_COUNT
#define LAYOUT_FIELD_COUNT 1024
#endif
#ifdef LAYOUT_TABLE_ALIGN
alignas(LAYOUT_TABLE_ALIGN)
#endif
extern constexpr auto layout_fields = layout_fixture::rows(std::make_index_sequence<LAYOUT_FIELD_COUNT>{});
extern constexpr telemetry::Catalog layout_catalogs[] = {
    {0, "values", layout_fields.data(), layout_fields.size()},
};
extern constexpr telemetry::CatalogIndex layout_index{layout_catalogs};
static_assert(layout_index.size() == 1 && layout_catalogs[0].count == LAYOUT_FIELD_COUNT);

#define LAYOUT_PROBE __attribute__((noinline))
extern "C" {
LAYOUT_PROBE const telemetry::Field* layout_find_runtime(
    const telemetry::CatalogIndex& index, telemetry::FieldId id) noexcept { return index.find(id); }
LAYOUT_PROBE telemetry::Scalar layout_read_runtime(
    const telemetry::CatalogIndex& index, telemetry::FieldId id) noexcept { return index.read(id); }
LAYOUT_PROBE float layout_read_runtime_float(
    const telemetry::CatalogIndex& index, telemetry::FieldId id) noexcept { return index.read<float>(id).value_or(0); }
LAYOUT_PROBE telemetry::WriteResult layout_write_runtime_float(
    const telemetry::CatalogIndex& index, telemetry::FieldId id, float value) noexcept { return index.write(id, value); }
LAYOUT_PROBE telemetry::WriteResult layout_write_runtime_u16(
    const telemetry::CatalogIndex& index, telemetry::FieldId id, std::uint16_t value) noexcept { return index.write(id, value); }
LAYOUT_PROBE telemetry::Scalar layout_read_fixed(telemetry::FieldId id) noexcept { return layout_index.read(id); }
LAYOUT_PROBE telemetry::WriteResult layout_write_fixed_float(telemetry::FieldId id, float value) noexcept { return layout_index.write(id, value); }
LAYOUT_PROBE telemetry::WriteResult layout_write_fixed_u16(telemetry::FieldId id, std::uint16_t value) noexcept { return layout_index.write(id, value); }
LAYOUT_PROBE float layout_read_known() noexcept { return layout_index.read<float>(0).value_or(0); }
LAYOUT_PROBE float layout_read_known_bounded() noexcept { return layout_index.read<float>(1).value_or(0); }
LAYOUT_PROBE telemetry::WriteResult layout_write_known(float value) noexcept { return layout_index.write(0, value); }
LAYOUT_PROBE telemetry::WriteResult layout_write_known_constant() noexcept { return layout_index.write(0, 250); }
LAYOUT_PROBE telemetry::WriteResult layout_write_restricted_f32(float value) noexcept { return layout_index.write(1, value); }
LAYOUT_PROBE telemetry::WriteResult layout_write_full_u16(std::uint16_t value) noexcept { return layout_index.write(2, value); }
LAYOUT_PROBE telemetry::WriteResult layout_write_restricted_u16(std::uint16_t value) noexcept { return layout_index.write(3, value); }
LAYOUT_PROBE std::uint16_t layout_enum_read() noexcept { return layout_index.read<std::uint16_t>(4).value_or(0); }
LAYOUT_PROBE std::uint16_t layout_plain_read() noexcept { return layout_index.read<std::uint16_t>(5).value_or(0); }
LAYOUT_PROBE telemetry::WriteResult layout_enum_write(std::uint16_t value) noexcept { return layout_index.write(4, value); }
LAYOUT_PROBE telemetry::WriteResult layout_plain_write(std::uint16_t value) noexcept { return layout_index.write(5, value); }
LAYOUT_PROBE std::uint16_t layout_read_normalized() noexcept { return layout_index.read<std::uint16_t>(6).value_or(0); }
LAYOUT_PROBE telemetry::WriteResult layout_write_readonly(float value) noexcept { return layout_index.write(7, value); }
}
#undef LAYOUT_PROBE
