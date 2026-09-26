// Review probe (numeric-core): the lib/telemetry/README.md "Enum dictionaries
// for schemas" example, compiled as written, plus its stated results.
#include "Telemetry.h"
#include "serialization/TelemetryJson.h"
#include <cstdio>
#include <cstring>
using namespace telemetry;
enum class Mode : std::uint16_t { Off, Auto, Manual };
Mode mode = Mode::Auto;
constexpr auto readMode = +[]() noexcept { return mode; };
constexpr auto writeMode = +[](Mode value) noexcept {
    mode = value;
    return WriteResult::Applied;
};
constexpr FieldTable settingsFields{
    field<readMode, writeMode>("Mode", "", limits(Mode::Auto)),
};
constexpr FieldCatalogTable fields{group("settings", settingsFields)};
constexpr auto index = fields.index();
int main()
{
    auto number = fields.read<makeId(0, 0)>();
    static_assert(std::is_same_v<decltype(number), std::optional<std::uint16_t>>);
    auto result = index.write(makeId(0, 0), 100);
    char schema[1024];
    writeSchema(index, schema, sizeof schema);
    const char* at = std::strstr(schema, "\"t\":");
    std::printf("read=%u write100=%s\nschema fragment: %.120s\n", unsigned(*number),
                result == WriteResult::InvalidValue ? "InvalidValue" : "other", at ? at : "(none)");
    return 0;
}
