// Critic trial: cost of including the core umbrella header in a translation unit that uses nothing.
#include "Telemetry.h"

int criticIncludeOnly() { return 0; }
