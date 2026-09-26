// Shared inline serializers must name the same functions in separate modules.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "detail/TelemetryJsonValue.h"
#include <cstdio>

struct JsonHelperAddresses {
    decltype(&telemetry::detail::scalarTypeName) type;
    decltype(&telemetry::detail::appendScalar) scalar;
    decltype(&telemetry::detail::appendBound) bound;
    decltype(&telemetry::detail::appendEnumEntry) enumeration;
    decltype(&telemetry::detail::appendMetadata) metadata;
};

#ifndef TELEMETRY_JSON_LINKAGE_PART
#define TELEMETRY_JSON_LINKAGE_PART 0
#endif

#if TELEMETRY_JSON_LINKAGE_PART == 1
JsonHelperAddresses jsonHelpersFromFirstModule() noexcept
#elif TELEMETRY_JSON_LINKAGE_PART == 2
JsonHelperAddresses jsonHelpersFromSecondModule() noexcept
#endif
#if TELEMETRY_JSON_LINKAGE_PART != 0
{
    return {&telemetry::detail::scalarTypeName, &telemetry::detail::appendScalar,
            &telemetry::detail::appendBound, &telemetry::detail::appendEnumEntry,
            &telemetry::detail::appendMetadata};
}
#else
JsonHelperAddresses jsonHelpersFromFirstModule() noexcept;
JsonHelperAddresses jsonHelpersFromSecondModule() noexcept;

int main()
{
    const auto first = jsonHelpersFromFirstModule();
    const auto second = jsonHelpersFromSecondModule();
    const bool same = first.type == second.type && first.scalar == second.scalar
        && first.bound == second.bound && first.enumeration == second.enumeration
        && first.metadata == second.metadata;
    std::printf("JSON helper linkage: %s\n", same ? "passed" : "different module identities");
    return same ? 0 : 1;
}
#endif
