// Compile-only Cortex-M7 probe. Inspect the object with arm-none-eabi-objdump.
// From the telemetry repository root, use the CubeIDE compiler and these options:
// -std=c++17 -O2 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
// -fno-exceptions -fno-rtti -Wall -Wextra -Werror
// -Ilib/telemetry -Ilib/delegate
// -c tests/IndexCodegen.cpp -o build/read_arm_check/IndexCodegen-O2.o
// Repeat with -Os. Only the exported probe functions are forced out of line;
// the real lookup and getter implementations remain available for optimization.

#include "catalog/TelemetryIndex.h"

// Runtime sources are supplied by a consumer when linking. Leaving them
// undefined here separates index storage from application RAM in the object.
extern volatile float telemetry_probe_voltage;
extern volatile float telemetry_probe_current;
extern float telemetry_probe_threshold;

namespace {

using telemetry::makeId;
using telemetry::Scalar;

float read_voltage() noexcept
{
    return telemetry_probe_voltage;
}

telemetry::WriteResult write_threshold(const Scalar& value) noexcept
{
    if (value.type() != telemetry::ScalarType::F32) return telemetry::WriteResult::InvalidValue;
    telemetry_probe_threshold = value.get<float>();
    return telemetry::WriteResult::Applied;
}

} // namespace

// High 16 ID bits select the group; low 16 bits select its field. Both
// positions start at zero and have no gaps. No per-field pointer map exists.
extern constexpr telemetry::Field telemetry_probe_group0_fields[] = {
    {makeId(0, 0), "Ua", "V", telemetry::ScalarType::F32, read_voltage},
    {makeId(0, 1), "Ia", "A", telemetry::ScalarType::F32,
     +[]() noexcept { return telemetry_probe_current; }},
    {makeId(0, 2), "P", "kW", telemetry::ScalarType::F32,
     []() noexcept {
         return Scalar::fromF32(telemetry_probe_voltage * telemetry_probe_current / 1000.0f);
     }},
    {makeId(0, 3), "UaBind", "V", telemetry::ScalarType::F32,
     telemetry::Getter::bind<&read_voltage>()},
};

extern constexpr telemetry::Field telemetry_probe_group1_fields[] = {
    {makeId(1, 0), "UaAgain", "V", telemetry::ScalarType::F32, read_voltage},
    {makeId(1, 1), "IaAgain", "A", telemetry::ScalarType::F32,
     []() noexcept { return Scalar::fromF32(telemetry_probe_current); }},
    {makeId(1, 2), "Limit", "V", telemetry::ScalarType::F32,
     []() noexcept { return telemetry_probe_threshold; },
     telemetry::Setter::bind<&write_threshold>()},
};

extern constexpr telemetry::Catalog telemetry_probe_catalogs[] = {
    {0, "electrical", telemetry_probe_group0_fields},
    {1, "aux", telemetry_probe_group1_fields},
};

extern constexpr telemetry::CatalogIndex telemetry_probe_index{
    telemetry_probe_catalogs};
constexpr auto telemetry_probe_static_index =
    telemetry::CatalogIndex::bind<telemetry_probe_catalogs>();

static_assert(makeId(65535, 65535) == UINT32_MAX);
static_assert(telemetry_probe_catalogs[0].count == 4);
static_assert(telemetry_probe_catalogs[1].count == 3);
static_assert(telemetry_probe_index.size() == 2);
static_assert(telemetry_probe_index.find(makeId(1, 0)) == &telemetry_probe_group1_fields[0]);
static_assert(telemetry_probe_index.find(makeId(2, 0)) == nullptr);
static_assert(telemetry_probe_index.find(makeId(0, 4)) == nullptr);

extern "C" {

// The index argument prevents folding its address, group count or metadata.
__attribute__((noinline)) const telemetry::Field* telemetry_probe_find_generic(
    const telemetry::CatalogIndex& index, telemetry::FieldId id) noexcept
{
    return index.find(id);
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_find_fixed(
    telemetry::FieldId id) noexcept
{
    return telemetry_probe_index.find(id);
}

__attribute__((noinline)) const telemetry::Catalog* telemetry_probe_catalog_fixed(
    telemetry::GroupId group) noexcept
{
    return telemetry_probe_index.catalog(group);
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_find_known() noexcept
{
    return telemetry_probe_index.find(makeId(1, 0));
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_missing_group() noexcept
{
    return telemetry_probe_index.find(makeId(2, 0));
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_missing_row() noexcept
{
    return telemetry_probe_index.find(makeId(0, 4));
}

__attribute__((noinline)) Scalar telemetry_probe_read_function_known() noexcept
{
    return telemetry_probe_index.read(makeId(0, 0));
}

// Native consumers: verify whether the optional and Scalar intermediates
// disappear when the table, getter alternative and declared type are known.
__attribute__((noinline)) float telemetry_probe_read_explicit_known() noexcept
{
    return telemetry_probe_index.read<float>(makeId(0, 0)).value_or(-1.0f);
}

__attribute__((noinline)) float telemetry_probe_read_inferred_known() noexcept
{
    return telemetry_probe_static_index.read<makeId(0, 0)>().value_or(-1.0f);
}

__attribute__((noinline)) float telemetry_probe_field_read_native() noexcept
{
    return telemetry_probe_group0_fields[0].read<float>().value_or(-1.0f);
}

__attribute__((noinline)) float telemetry_probe_static_read_native() noexcept
{
    return telemetry_probe_static_index.read<makeId(0, 0), float>().value_or(-1.0f);
}

__attribute__((noinline)) std::uint32_t telemetry_probe_field_read_converted() noexcept
{
    return telemetry_probe_group0_fields[0].read<std::uint32_t>().value_or(0);
}

__attribute__((noinline)) std::uint32_t telemetry_probe_static_read_converted() noexcept
{
    return telemetry_probe_static_index.read<makeId(0, 0), std::uint32_t>().value_or(0);
}

__attribute__((noinline)) float telemetry_probe_read_missing_typed() noexcept
{
    return telemetry_probe_index.read<float>(makeId(2, 0)).value_or(-1.0f);
}

__attribute__((noinline)) std::uint32_t telemetry_probe_read_converted_known() noexcept
{
    return telemetry_probe_index.read<std::uint32_t>(makeId(0, 0)).value_or(0);
}

__attribute__((noinline)) Scalar telemetry_probe_read_lambda_known() noexcept
{
    const telemetry::Field* field = telemetry_probe_index.find(makeId(0, 1));
    return field != nullptr ? field->get() : Scalar::null();
}

__attribute__((noinline)) Scalar telemetry_probe_read_bound_known() noexcept
{
    const telemetry::Field* field = telemetry_probe_index.find(makeId(0, 3));
    return field != nullptr ? field->get() : Scalar::null();
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_write_known_constant() noexcept
{
    return telemetry_probe_index.write(makeId(1, 2), 250);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_write_known_u16(std::uint16_t value) noexcept
{
    return telemetry_probe_index.write(makeId(1, 2), value);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_write_readonly() noexcept
{
    return telemetry_probe_index.write(makeId(0, 0), 250);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_write_missing() noexcept
{
    return telemetry_probe_index.write(makeId(2, 0), 250);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_field_write_float(
    float value) noexcept
{
    return telemetry_probe_group1_fields[2].write(value);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_static_write_float(
    float value) noexcept
{
    return telemetry_probe_static_index.write<makeId(1, 2)>(value);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_field_write_u16(
    std::uint16_t value) noexcept
{
    return telemetry_probe_group1_fields[2].write(value);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_static_write_u16(
    std::uint16_t value) noexcept
{
    return telemetry_probe_static_index.write<makeId(1, 2)>(value);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_field_write_readonly(
    float value) noexcept
{
    return telemetry_probe_group0_fields[0].write(value);
}

__attribute__((noinline)) telemetry::WriteResult telemetry_probe_static_write_readonly(
    float value) noexcept
{
    return telemetry_probe_static_index.write<makeId(0, 0)>(value);
}

} // extern "C"
