// Immutable definitions prevent disagreement between cached read/write types.
#include "catalog/TelemetryCatalog.h"
#include <utility>
telemetry::Field field;
#if TELEMETRY_FIELD_FAIL_CASE == 1
void rejected() { field.readType = telemetry::ScalarType::U16; }
#elif TELEMETRY_FIELD_FAIL_CASE == 2
void rejected() { field.declaredType = telemetry::ScalarType::U16; }
#elif TELEMETRY_FIELD_FAIL_CASE == 3
void rejected() { field.get = nullptr; }
#elif TELEMETRY_FIELD_FAIL_CASE == 4
void rejected() { field.set = nullptr; }
#elif TELEMETRY_FIELD_FAIL_CASE == 5
void rejected() { field.id = 1; }
#elif TELEMETRY_FIELD_FAIL_CASE == 6
void rejected() { field.name = "changed"; }
#elif TELEMETRY_FIELD_FAIL_CASE == 7
void rejected() { field.unit = "changed"; }
#elif TELEMETRY_FIELD_FAIL_CASE == 8
void rejected() { telemetry::Field other; field = other; }
#elif TELEMETRY_FIELD_FAIL_CASE == 9
void rejected() { telemetry::Field other; field = std::move(other); }
#else
#error Select TELEMETRY_FIELD_FAIL_CASE from 1 to 9
#endif
