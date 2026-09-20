#include "Fixture.h"
#include "serialization/TelemetryJson.h"
#include <cstdio>
#include <type_traits>
#include <utility>

volatile float layout_source_f32 = 230;
volatile double layout_source_f64 = 12.75;
volatile std::uint16_t layout_source_u16 = 17;
volatile float layout_sink_f32 = 0;
volatile std::uint16_t layout_sink_u16 = 0;

using namespace telemetry;
constexpr Field empty;
constexpr Field partial{LAYOUT_ID(12) "partial"};
constexpr Field copy = partial;
constexpr Field moved = std::move(copy);
static_assert(empty.declaredType == ScalarType::Null && !empty.get && !empty.set);
static_assert(partial.name[0] == 'p' && moved.name == partial.name);
static_assert(std::is_trivially_copyable_v<Field>);
static_assert(std::is_trivially_copy_constructible_v<Field> && std::is_trivially_move_constructible_v<Field>);
static_assert(std::is_copy_constructible_v<Field> && std::is_move_constructible_v<Field>);
#if TELEMETRY_LAYOUT_VARIANT == 2 || TELEMETRY_LAYOUT_VARIANT == 6
static_assert(!std::is_copy_assignable_v<Field> && !std::is_move_assignable_v<Field>);
#if TELEMETRY_LAYOUT_VARIANT == 2
static_assert(!std::is_assignable_v<decltype((std::declval<Field&>().type)), ScalarType>);
static_assert(!std::is_assignable_v<decltype((std::declval<Field&>().flags)), std::uint8_t>);
#else
static_assert(!std::is_assignable_v<decltype((std::declval<Field&>().readType)), ScalarType>);
#endif
static_assert(!std::is_assignable_v<decltype((std::declval<Field&>().declaredType)), FieldType>);
static_assert(!std::is_assignable_v<decltype((std::declval<Field&>().get)), Getter>);
static_assert(!std::is_assignable_v<decltype((std::declval<Field&>().set)), Setter>);
static_assert(!std::is_aggregate_v<Field>);
#elif TELEMETRY_LAYOUT_VARIANT == 1 || TELEMETRY_LAYOUT_VARIANT >= 4
static_assert(!std::is_aggregate_v<Field> && std::is_copy_assignable_v<Field>);
#else
static_assert(std::is_aggregate_v<Field> && std::is_copy_assignable_v<Field>);
#endif

constexpr auto rows = layout_fixture::rows(std::make_index_sequence<8>{});
constexpr Catalog catalogs[] = {{LAYOUT_ID(0) "values", rows.data(), rows.size()}};
constexpr auto index = CatalogIndex::bind<catalogs>();
static_assert(names_unique(rows.data(), rows.size()));
static_assert(rows[4].declaredType.hasEnum());
static_assert(rows[1].declaredType.minimum().get<float>() == 1);
static_assert(rows[1].declaredType.maximum().get<float>() == 1000);
static_assert(rows[1].declaredType.defaultValue().get<float>() == 250);

int main()
{
    const Field localCopy = rows[1];
    Field moveSource = localCopy;
    const Field localMove = std::move(moveSource);
    if (localMove.name != localCopy.name || localMove.declaredType != localCopy.declaredType) return 5;
    if (localCopy.write(300) != WriteResult::Applied || layout_sink_f32 != 300) return 1;
    if (localCopy.write(1001) != WriteResult::InvalidValue || layout_sink_f32 != 300) return 2;
    if (localMove.write(350) != WriteResult::Applied || layout_sink_f32 != 350) return 6;
    if (index.read<6>().value_or(0) != 12 || index.write(7, 1) != WriteResult::ReadOnly) return 3;
    char schema[4096], values[256];
    if (!writeSchema(index, schema, sizeof(schema)) || !writeValues(index, values, sizeof(values))) return 4;
    // The runner compares complete schema/value documents between all variants.
    std::puts(schema);
    std::puts(values);
}
