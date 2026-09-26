// Regression promoted from tests/review/catalog-json/AbiGcSections.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review probe (catalog-json): does the link-time ABI guard still fire when
// the only reference to the tagged anchor lives in a function that the final
// link discards (-ffunction-sections + --gc-sections)?
// Build this caller with a different TELEMETRY_FORCE_CACHELINE than the archive.
#include "Telemetry.h"

extern "C" void telemetry_unused_abi_check() noexcept
{
    telemetry::requireTelemetryAbi(); // never called from main
}

int main()
{
#if CALL_FROM_MAIN
    telemetry::requireTelemetryAbi();
#endif
    return 0;
}
