// Schema wire compatibility across the positional ABI migration.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>
using namespace telemetry;
enum class Mode : std::uint16_t { Off, Auto, Manual };
float voltage() noexcept { return 230.25f; }
Mode mode() noexcept { return Mode::Auto; }
WriteResult setMode(Mode) noexcept { return WriteResult::Applied; }
CommandResult configure(float, Mode) noexcept { return CommandResult::Executed; }

#if TELEMETRY_BASELINE_IDS
constexpr Field rawFields[] = {
    makeField<&voltage>(makeId(0, 0), "Ua", "V"),
    makeField<&mode, &setMode>(makeId(0, 1), "Mode", "", limits(Mode::Auto))};
constexpr Catalog groups[] = {{0, "meter", rawFields}};
constexpr CatalogIndex fields{groups};
constexpr auto args = commandArgs(arg<0>("Voltage", "V", 250.f, 1.f, 1000.f),
                                  arg<1>("Mode", "", Mode::Auto));
constexpr Command rawCommands[] = {makeCommand<&configure>(makeId(0, 0), "Configure", args)};
constexpr CommandCatalog commandGroups[] = {{0, "meter", rawCommands}};
constexpr CommandCatalogIndex commands{commandGroups};
#else
constexpr FieldTable meterFields{
    field<&voltage>("Ua", "V"),
    field<&mode, &setMode>("Mode", "", limits(Mode::Auto))};
constexpr FieldCatalogTable fields{group("meter", meterFields)};
constexpr CommandTable meterCommands{
    command<&configure>("Configure", arg<0>("Voltage", "V", 250.f, 1.f, 1000.f),
                        arg<1>("Mode", "", Mode::Auto))};
constexpr CommandCatalogTable commands{group("meter", meterCommands)};
#endif

int main()
{
    char output[4096];
    if (!writeSchema(fields, output, sizeof output)) return 1;
    std::puts(output);
    if (!writeValues(fields, output, sizeof output)) return 2;
    std::puts(output);
    if (!writeSchema(commands, output, sizeof output)) return 3;
    std::puts(output);
}
