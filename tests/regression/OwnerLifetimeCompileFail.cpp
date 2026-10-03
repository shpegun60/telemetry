// Original shared owner proxy lifetime case 14 (MIT).
#include <telemetry/slot/TelemetryOwnerSlot.h>
struct Owner { int value = 9; };
struct Holder { Owner inner; operator const Owner&() const noexcept { return inner; } };
#if CASE == 14
telemetry::OwnerSlot<const Owner> slot;
void probe() { slot.bind(Holder{}); }
#else
#error "Select maintained owner lifetime case 14"
#endif
int main() {}
