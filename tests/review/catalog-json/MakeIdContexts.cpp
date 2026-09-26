// Review probe (catalog-json): where does each compiler diagnose makeId(0, 65537)?
#include "core/TelemetryId.h"
template <telemetry::PackedId Id> constexpr telemetry::PackedId echo() { return Id; }
telemetry::PackedId runtimeContext() { return telemetry::makeId(0, 65537); }            // ordinary call
constexpr telemetry::PackedId constantContext = telemetry::makeId(0, 65537);             // constexpr init
constexpr telemetry::PackedId templateContext = echo<telemetry::makeId(0, 65537)>();      // template argument
static_assert(templateContext == 1 && constantContext == 1, "65537 wrapped to entry 1");
