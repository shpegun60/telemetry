#include "TelemetryCatalog.h"
#include <utility>
telemetry::Field field;
#if LAYOUT_FAIL_CASE == 1
void rejected() { field.type = telemetry::ScalarType::U16; }
#elif LAYOUT_FAIL_CASE == 2
void rejected() { field.flags = 0; }
#elif LAYOUT_FAIL_CASE == 3
void rejected() { field.declaredType = telemetry::ScalarType::U16; }
#elif LAYOUT_FAIL_CASE == 4
void rejected() { telemetry::Field other; field = other; }
#elif LAYOUT_FAIL_CASE == 5
void rejected() { telemetry::Field other; field = std::move(other); }
#else
#error Select LAYOUT_FAIL_CASE from 1 to 5
#endif
