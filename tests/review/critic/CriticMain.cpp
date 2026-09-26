// Critic trial consumer (C++17 part): fields, commands, JSON schema and values.
// Built by hand with plain compiler invocations; see Notes.txt in this directory.
#include "CriticDevice.h"
#include "serialization/TelemetryCommandJson.h"
#include "serialization/TelemetryJson.h"

#include <cstdio>

namespace {
char schemaJson[4096];
char valuesJson[1024];
char commandJson[2048];
#ifdef CRITIC_WITH_RESOURCES
char resourceText[8192];
#endif

const char* writeName(telemetry::WriteResult r)
{
    switch (r) {
    case telemetry::WriteResult::Applied: return "Applied";
    case telemetry::WriteResult::NotFound: return "NotFound";
    case telemetry::WriteResult::ReadOnly: return "ReadOnly";
    case telemetry::WriteResult::InvalidValue: return "InvalidValue";
    case telemetry::WriteResult::Busy: return "Busy";
    case telemetry::WriteResult::Unavailable: return "Unavailable";
    }
    return "?";
}

const char* commandName(telemetry::CommandResult r)
{
    switch (r) {
    case telemetry::CommandResult::Executed: return "Executed";
    case telemetry::CommandResult::Accepted: return "Accepted";
    case telemetry::CommandResult::NotFound: return "NotFound";
    case telemetry::CommandResult::Unavailable: return "Unavailable";
    case telemetry::CommandResult::ArgumentCountMismatch: return "ArgumentCountMismatch";
    case telemetry::CommandResult::InvalidValue: return "InvalidValue";
    case telemetry::CommandResult::Busy: return "Busy";
    case telemetry::CommandResult::Failed: return "Failed";
    }
    return "?";
}
} // namespace

int main(int argc, char** argv)
{
    using namespace critic;
    using telemetry::makeId;
#ifdef CRITIC_WITH_RESOURCES
    if (argc > 1) return saveResources(argv[1]);
#else
    (void)argc;
    (void)argv;
#endif

    // Compile-time local access.
    auto ua = meterFields.read<0>();
    std::printf("local read Ua = %g\n", ua ? static_cast<double>(*ua) : -1.0);

    // Runtime access as a transport would do it.
    std::printf("write OverVoltage=275 -> %s\n", writeName(fieldIndex.write(makeId(0, 5), 275)));
    std::printf("write OverVoltage=400 -> %s\n", writeName(fieldIndex.write(makeId(0, 5), 400)));
    std::printf("write Ua (read-only)  -> %s\n", writeName(fieldIndex.write(makeId(0, 0), 1)));
    std::printf("write Mode=7          -> %s\n", writeName(fieldIndex.write(makeId(0, 6), 7)));
    std::printf("write missing id      -> %s\n", writeName(fieldIndex.write(makeId(3, 0), 1)));

    const telemetry::Scalar configureArgs[] = {250.0f, std::uint8_t{2}};
    std::printf("execute Configure     -> %s\n",
                commandName(commandIndex.execute(makeId(0, 1), configureArgs, 2)));
    std::printf("native Reset          -> %s\n", commandName(meterCommands.call<0>()));
    std::printf("native Configure bad  -> %s\n", commandName(meterCommands.call<1>(900.0, 1)));

    const auto schemaLength = telemetry::writeSchema(fieldIndex, schemaJson, sizeof(schemaJson));
    const auto valuesLength = telemetry::writeValues(fieldIndex, valuesJson, sizeof(valuesJson));
    const auto commandLength = telemetry::writeSchema(commandIndex, commandJson, sizeof(commandJson));
    std::printf("schema (%u bytes): %s\n", static_cast<unsigned>(schemaLength), schemaJson);
    std::printf("values (%u bytes): %s\n", static_cast<unsigned>(valuesLength), valuesJson);
    std::printf("commands (%u bytes): %s\n", static_cast<unsigned>(commandLength), commandJson);

#ifdef CRITIC_WITH_RESOURCES
    const auto resourceLength = dumpResources(resourceText, sizeof(resourceText));
    std::printf("resources (%u chars):\n%s\n", static_cast<unsigned>(resourceLength), resourceText);
#endif
    return schemaLength && valuesLength && commandLength ? 0 : 1;
}
