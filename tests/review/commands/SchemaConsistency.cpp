// Review probe (commands slice): does the command JSON schema describe exactly
// what execute() accepts, for each parameter shape?
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>
#include <cstring>
#include <limits>

using namespace telemetry;

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* label)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
enum class Mode : std::uint8_t { Off, Auto, Manual };
CommandResult flag(bool) noexcept { return CommandResult::Executed; }
CommandResult wide(double, std::uint64_t, std::int64_t) noexcept { return CommandResult::Executed; }
CommandResult mode(Mode) noexcept { return CommandResult::Executed; }
FunctionSlot<CommandResult(std::int8_t) noexcept> slot;

constexpr CommandTable rows{
    command<&flag>("flag"),
    command<&wide>("wide", arg<0>("d", "", 1.5, -2.5, 10.25),
                   arg<1>("u", "", UINT64_MAX, std::uint64_t{1}, UINT64_MAX)),
    command<&mode>("bounded enum", arg<0>("m", "", Mode::Auto, Mode::Auto, Mode::Manual)),
    command<&mode>("subset enum", arg<0>("m", "", enumSpec<Mode::Off, Mode::Manual>())),
    reservedCommand(),
    command("slot", slot),
};

bool run(std::size_t id, const Scalar& value)
{
    return rows.index().execute(static_cast<CommandId>(id), &value, 1) == CommandResult::Executed;
}
} // namespace

int main()
{
    char json[4096], strings[4096];
    expect(writeSchema(rows.index(), json, sizeof json) != 0, "schema written");
    expect(writeSchema(rows.index(), strings, sizeof strings, {JsonInt64Mode::String}) != 0, "string mode");
    std::printf("%s\n\n%s\n\n", json, strings);
    expect(std::strncmp(json, strings, 20) == 0, "CRC independent of 64-bit mode");

    // Bounded enum: schema says min 1 max 2, but its dictionary still lists 0 ("Off").
    expect(std::strstr(json, "\"n\":\"bounded enum\",\"params\":[{\"i\":0,\"n\":\"m\",\"u\":\"\",\"t\":\"u8\",\"min\":1,\"max\":2,\"default\":1,\"enum\":{\"0\":\"Off\"") != nullptr,
           "bounded enum lists an out-of-bounds name");
    expect(!run(2, std::uint8_t{0}) && run(2, std::uint8_t{1}) && run(2, std::uint8_t{2}) && !run(2, std::uint8_t{3}),
           "bounded enum validation follows min/max, not the dictionary");
    // Subset: dictionary {0,2}, but code 1 (a named enumerator outside the subset) executes.
    expect(std::strstr(json, "\"enum\":{\"0\":\"Off\",\"2\":\"Manual\"}") != nullptr, "subset dictionary");
    expect(run(3, std::uint8_t{1}), "subset enum accepts excluded named code 1 (documented interval rule)");
    // Wide limits.
    expect(run(1, 1.0) == false, "wide is 3-arity; single value is a count mismatch");
    const Scalar ok3[] = {10.25, std::uint64_t{1}, std::int64_t{INT64_MIN}};
    const Scalar bad3[] = {10.26, std::uint64_t{1}, std::int64_t{0}};
    const Scalar zero3[] = {0.0, std::uint64_t{0}, std::int64_t{0}};
    expect(rows.index().execute(1, ok3, 3) == CommandResult::Executed, "wide ok at inclusive bounds");
    expect(rows.index().execute(1, bad3, 3) == CommandResult::InvalidValue, "double max inclusive");
    expect(rows.index().execute(1, zero3, 3) == CommandResult::InvalidValue, "u64 min 1");
    // Reserved and unbound slot rows are visible and look like commands.
    expect(std::strstr(json, "{\"id\":4,\"n\":\"\",\"params\":[]}") != nullptr, "reserved row shape");
    expect(std::strstr(json, "\"n\":\"slot\",\"params\":[{\"i\":0,\"t\":\"s8\",\"min\":null,\"max\":null,\"default\":0}]") != nullptr,
           "unbound slot row keeps its parameter schema");
    std::printf("%d/%d schema checks passed\n", checks - failures, checks);
    return failures != 0;
}
