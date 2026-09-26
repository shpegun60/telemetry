// Review probe (commands slice): Command descriptor size on ARM32 (README says 20; tests/README says 24).
#include "Telemetry.h"
static_assert(sizeof(telemetry::Command) == 20, "Command is not 20 bytes");
static_assert(alignof(telemetry::Command) == 4);
int main(){}
