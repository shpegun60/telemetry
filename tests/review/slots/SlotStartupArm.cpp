// Review probe (slots): what a namespace-scope definition of each slot kind
// costs at startup on Cortex-M7. The shipped codegen probes keep every slot
// extern, so they cannot see this. Compile only (-c) with the ARM flags from
// tests/run_arm_checks.py, then inspect sections and symbols.
#include "Telemetry.h"
using namespace telemetry;

struct StartupMeter { float read() const noexcept; };
extern StartupMeter startupMeter;

#if defined(PROBE_OWNER)
OwnerSlot<StartupMeter> probeOwner;
constexpr FieldTable probeRows{field<&StartupMeter::read>("V", "", probeOwner)};
#elif defined(PROBE_FUNCTION)
FunctionSlot<float() noexcept> probeFunction;
constexpr FieldTable probeRows{field("V", "", probeFunction)};
#elif defined(PROBE_CONTEXT)
ContextFunctionSlot<float() noexcept> probeContext;
constexpr FieldTable probeRows{field("V", "", probeContext)};
#elif defined(PROBE_REF)
DelegateRefSlot<float() noexcept> probeRef;
constexpr FieldTable probeRows{field("V", "", probeRef)};
#elif defined(PROBE_OWNED)
DelegateSlot<float() noexcept> probeOwned;
constexpr FieldTable probeRows{field("V", "", probeOwned)};
// bind() instantiates the delegate's manager table (a function-local static).
extern "C" void probe_bind() noexcept
{
    probeOwned.bind([]() noexcept { return startupMeter.read(); });
}
#else
#error define one PROBE_* kind
#endif

extern "C" float probe_read() noexcept { return probeRows.read<0>().value_or(-1.f); }
extern "C" unsigned probe_slot_size() noexcept
{
#if defined(PROBE_OWNER)
    return sizeof(probeOwner);
#elif defined(PROBE_FUNCTION)
    return sizeof(probeFunction);
#elif defined(PROBE_CONTEXT)
    return sizeof(probeContext);
#elif defined(PROBE_REF)
    return sizeof(probeRef);
#else
    return sizeof(probeOwned);
#endif
}
