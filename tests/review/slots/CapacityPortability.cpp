// Review probe (slots): default DelegateSlot capacity differs between an x64 host
// (Align 16, 8-byte pointers) and Cortex-M7 (Align 8, 4-byte pointers).
#include "Telemetry.h"
using namespace telemetry;
struct alignas(16) Vec4 { float v[4]; };
DelegateSlot<float() noexcept> slot;
void bindAligned() { Vec4 q{}; slot.bind([q]() noexcept { return q.v[0]; }); }            // 16 B, align 16
void bindFiveRefs(float& a, float& b, float& c, float& d, float& e)
{ slot.bind([&a, &b, &c, &d, &e]() noexcept { return a + b + c + d + e; }); }             // 5 pointers
int main() {}
