// Review repro (slots): the documented prohibition "an owned callback must not
// reset or replace its own slot while executing" (slot/README.md). This shows
// the consequence: the executing closure keeps running in storage that now
// holds the replacement, and reads the replacement's captured state. Nothing
// traps, and ASan/UBSan stay silent because the storage is reused in place.
// This is deliberately undefined behavior; it is a demonstration, not a test.
#include "Telemetry.h"
#include <cstdio>
using namespace telemetry;

DelegateSlot<float() noexcept> slot;

int main()
{
    const float original = 1.f;
    slot.bind([captured = original]() noexcept {
        const float before = captured;
        slot.bind([other = 1000.f]() noexcept { return other; }); // replaces *this
        const volatile float after = captured; // reads the replacement's bytes
        std::printf("inside the old callback: capture before=%g after=%g\n", before, after);
        return after;
    });
    const float result = slot.invoke();
    std::printf("returned %g; next call returns %g\n", result, slot.invoke());
    return 0;
}
