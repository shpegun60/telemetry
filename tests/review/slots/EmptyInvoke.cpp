// Review repro (slots): direct invoke() on an empty slot, one kind per process.
// Usage: EmptyInvoke owned|ref|function|context|ownedview
// The slot README claims delegate slots terminate (trap) instead of calling
// null; function/context slots document invoke() as precondition-only.
#include "Telemetry.h"
#include <cstdio>
#include <cstring>
using namespace telemetry;

DelegateSlot<float() noexcept> owned;
DelegateRefSlot<float() noexcept> ref;
FunctionSlot<float() noexcept> function;
ContextFunctionSlot<float() noexcept> context;

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    std::printf("invoking empty %s\n", argv[1]);
    std::fflush(stdout);
    float value = 0.f;
    if (!std::strcmp(argv[1], "owned")) value = owned.invoke();
    else if (!std::strcmp(argv[1], "ref")) value = ref.invoke();
    else if (!std::strcmp(argv[1], "function")) value = function.invoke();
    else if (!std::strcmp(argv[1], "context")) value = context.invoke();
    else if (!std::strcmp(argv[1], "ownedview")) { auto view = owned.get(); value = view.invoke(); }
    else return 2;
    std::printf("returned normally: %g\n", value);
    return 0;
}
