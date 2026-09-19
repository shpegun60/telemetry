// Minimal C-library integration probe, suitable for a Cortex-M link check.
// Use IndexCodegen.cpp's compiler flags, then add --specs=nano.specs,
// --specs=nosys.specs and -Wl,-u,_printf_float when linking the JSON module.
// Linking this program does not execute it or establish board behavior.
#include "serialization/TelemetryJson.h"
#include "field/TelemetryEnum.h"
#include <cstring>

using namespace telemetry;
enum class Mode : std::uint16_t { Off, Auto, Manual };
constexpr Field fields[] = {
    {0, "u64", "", ScalarType::U64, []() noexcept { return UINT64_MAX; }},
    {1, "s64", "", ScalarType::S64, []() noexcept { return INT64_MIN; }},
    {2, "f32", "", ScalarType::F32, []() noexcept { return 1.5f; }},
    {3, "mode", "", enumType<Mode>(), []() noexcept { return std::uint16_t{1}; }},
};
constexpr Catalog catalogs[] = {{0, "v", fields}};

int main()
{
    char text[1024];
    if (writeValues(catalogs, 1, text, sizeof(text)) == 0) return 1;
    if (std::strcmp(text, "{\"v\":[18446744073709551615,-9223372036854775808,1.5,1]}") != 0) return 2;
    if (writeSchema(catalogs, 1, text, sizeof(text)) == 0) return 3;
    return std::strstr(text, "\"enum\":{\"0\":\"Off\",\"1\":\"Auto\",\"2\":\"Manual\"}") == nullptr;
}
