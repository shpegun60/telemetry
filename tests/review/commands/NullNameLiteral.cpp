// Review probe (commands slice): TelemetryTableCompileFail case 24 rejects
// command<&run>(0, "run") only because "run" is not metadata; the literal 0 is a
// null-pointer constant for the name. The one-argument form compiles and yields a
// descriptor whose name is null: it executes, but schema export fails (returns 0).
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>
telemetry::CommandResult run() noexcept { return telemetry::CommandResult::Executed; }
constexpr telemetry::CommandTable rows{telemetry::command<&run>(0)};
static_assert(rows[0].name == nullptr);
int main()
{
    char json[256];
    std::printf("execute=%d schemaBytes=%zu crc=%08lx namesUnique=%d\n",
                static_cast<int>(rows.index().execute(0, nullptr, 0)),
                telemetry::writeSchema(rows.index(), json, sizeof json),
                static_cast<unsigned long>(telemetry::schemaCrc(rows.index())),
                telemetry::commandNamesUnique(rows.data(), rows.size()));
    return 0;
}
