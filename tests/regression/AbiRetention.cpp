// Retained ABI references, compiler omission and header-only controls.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"

#ifndef ABI_RETENTION_FORM
#define ABI_RETENTION_FORM 0
#endif
#ifndef ABI_RETENTION_NAME
#define ABI_RETENTION_NAME telemetry_abi_retention_probe
#endif

#if ABI_RETENTION_FORM == 1
TELEMETRY_RETAIN_ABI()
#elif ABI_RETENTION_FORM == 2
// This control has no retention guarantee if the compiler omits its caller.
[[maybe_unused]] static void omittedCaller() noexcept
{
    telemetry::requireTelemetryAbi();
}
#elif ABI_RETENTION_FORM == 3
extern "C" void ABI_RETENTION_NAME() noexcept
{
    telemetry::requireTelemetryAbi();
}
#elif ABI_RETENTION_FORM == 4
// Force compiler emission while still leaving the function unreferenced by
// any live code. Linker GC must not hide this object's ABI mismatch.
extern "C" __attribute__((used)) void ABI_RETENTION_NAME() noexcept
{
    telemetry::requireTelemetryAbi();
}
#endif

#ifndef ABI_RETENTION_NO_MAIN
int main()
{
#if ABI_RETENTION_FORM == 5
    telemetry::requireTelemetryAbi();
#endif
    return 0;
}
#endif
