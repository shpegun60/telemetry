// Review probe (slots): C++20 constinit for every slot kind (static-init order).
#include "Telemetry.h"
using namespace telemetry;
struct M { float read() const noexcept { return 1.f; } };
constinit OwnerSlot<M> ownerSlot;
constinit FunctionSlot<float() noexcept> functionSlot;
constinit ContextFunctionSlot<float() noexcept> contextSlot;
constinit DelegateRefSlot<float() noexcept> refSlot;
constinit DelegateSlot<float() noexcept> ownedSlot;
int main() { return ownerSlot || functionSlot || contextSlot || refSlot || ownedSlot; }
