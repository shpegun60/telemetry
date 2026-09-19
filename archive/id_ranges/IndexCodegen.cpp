// Compile-only Cortex-M7 probe. Inspect the object with arm-none-eabi-objdump.
// From the workspace root, use the CubeIDE compiler and these options:
// -std=c++17 -O2 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
// -fno-exceptions -fno-rtti -Wall -Wextra -Werror
// -Itelemetry/lib/telemetry -Itelemetry/lib/delegate
// -c telemetry/tests/IndexCodegen.cpp -o telemetry/build/id_check/index-O2.o
// Repeat with -Os. Only the exported probe functions are forced out of line;
// the real lookup and getter implementations remain available for optimization.

#include "TelemetryIndex.h"

// Runtime sources are supplied by a consumer when linking. Leaving them
// undefined here separates index storage from application RAM in the object.
extern volatile float telemetry_probe_voltage;
extern volatile float telemetry_probe_current;

namespace {

using telemetry::Scalar;

Scalar read_voltage() noexcept
{
    return Scalar::fromF32(telemetry_probe_voltage);
}

} // namespace

extern constexpr telemetry::Field telemetry_probe_dense_fields[] = {
    {1000, "Ua", "V", telemetry::ScalarType::F32, read_voltage},
    {1001, "Ia", "A", telemetry::ScalarType::F32,
     +[]() noexcept { return Scalar::fromF32(telemetry_probe_current); }},
    {1002, "P", "kW", telemetry::ScalarType::F32,
     []() noexcept {
         return Scalar::fromF32(telemetry_probe_voltage * telemetry_probe_current / 1000.0f);
     }},
    {1003, "UaBind", "V", telemetry::ScalarType::F32,
     telemetry::Getter::bind<&read_voltage>()},
};

// Explicit IDs are intentionally out of order, with unoccupied IDs between.
extern constexpr telemetry::Field telemetry_probe_sparse_fields[] = {
    {2004, "Ia", "A", telemetry::ScalarType::F32,
     []() noexcept { return Scalar::fromF32(telemetry_probe_current); }},
    {2000, "Ua", "V", telemetry::ScalarType::F32, read_voltage},
    {2002, "UaAgain", "V", telemetry::ScalarType::F32, read_voltage},
};

extern constexpr telemetry::Catalog telemetry_probe_catalogs[] = {
    {"unsorted", telemetry_probe_sparse_fields, 3},
};

extern constexpr telemetry::FieldRange<1000, 4> telemetry_probe_range{
    telemetry_probe_dense_fields};
extern constexpr telemetry::FieldIndex<2000, 8> telemetry_probe_index{
    telemetry_probe_catalogs, 1};

static_assert(telemetry_probe_range.valid());
static_assert(telemetry_probe_index.valid() && telemetry_probe_index.size() == 3);
static_assert(telemetry_probe_range.find(1000) == &telemetry_probe_dense_fields[0]);
static_assert(telemetry_probe_index.find(2002) == &telemetry_probe_sparse_fields[2]);
static_assert(telemetry_probe_index.find(2001) == nullptr);

extern "C" {

__attribute__((noinline)) const telemetry::Field* telemetry_probe_range_dynamic(
    telemetry::FieldId id) noexcept
{
    return telemetry_probe_range.find(id);
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_index_dynamic(
    telemetry::FieldId id) noexcept
{
    return telemetry_probe_index.find(id);
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_range_known() noexcept
{
    return telemetry_probe_range.find(1000);
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_index_known() noexcept
{
    return telemetry_probe_index.find(2002);
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_index_gap() noexcept
{
    return telemetry_probe_index.find(2001);
}

__attribute__((noinline)) const telemetry::Field* telemetry_probe_range_outside() noexcept
{
    return telemetry_probe_range.find(999);
}

__attribute__((noinline)) Scalar telemetry_probe_range_read_known() noexcept
{
    const telemetry::Field* field = telemetry_probe_range.find(1000);
    return field != nullptr ? field->get() : Scalar::null();
}

__attribute__((noinline)) Scalar telemetry_probe_index_read_known() noexcept
{
    const telemetry::Field* field = telemetry_probe_index.find(2002);
    return field != nullptr ? field->get() : Scalar::null();
}

__attribute__((noinline)) Scalar telemetry_probe_range_read_lambda() noexcept
{
    const telemetry::Field* field = telemetry_probe_range.find(1001);
    return field != nullptr ? field->get() : Scalar::null();
}

__attribute__((noinline)) Scalar telemetry_probe_range_read_bound() noexcept
{
    const telemetry::Field* field = telemetry_probe_range.find(1003);
    return field != nullptr ? field->get() : Scalar::null();
}

} // extern "C"
