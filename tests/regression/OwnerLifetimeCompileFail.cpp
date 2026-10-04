// Original shared owner proxy lifetime case 14 (MIT).
// Checks that a temporary conversion holder cannot supply a retained borrowed owner reference.
// The rejected reference points inside the holder and would expire at the end of the binding expression.

#include <telemetry/slot/TelemetryOwnerSlot.h>

// Owner state embedded inside the temporary conversion holder.
struct Owner {
	int value = 9;
};

// Temporary wrapper whose returned owner reference expires with the wrapper.
// Public methods:
// - operator const Owner&(): Expose borrowed subobject.
struct Holder {
	Owner inner;

	operator const Owner&() const noexcept
	{
		return inner;
	}
};
#if CASE == 14
telemetry::OwnerSlot<const Owner> slot;

void probe()
{
	slot.bind(Holder{});
}
#else
#error "Select maintained owner lifetime case 14"
#endif
int main()
{}
