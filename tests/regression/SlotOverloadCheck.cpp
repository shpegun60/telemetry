// A slot invokes the specialization selected for its declared signature.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <cstdio>

using namespace telemetry;

namespace {
struct DefaultedValue {
    template <class U> void operator()(U& value) const noexcept { value = 1; }
    void operator()(int, int = 0) const noexcept {}
};
struct EllipsisValue {
    template <class U> void operator()(U& value) const noexcept { value = 1; }
    void operator()(int, ...) const noexcept {}
};
struct VolatileValue {
    template <class U> void operator()(U& value) const noexcept { value = 1; }
    void operator()(int) volatile noexcept {}
};
struct MutableReference {
    template <class U> void operator()(U& value) noexcept { value = 1; }
    void operator()(int) const noexcept {}
};

template <class F>
bool check()
{
    F callable{};
    DelegateSlot<void(int&) noexcept> owned;
    DelegateRefSlot<void(int&) noexcept> borrowed;
    int value = 0;
    owned.bind(callable);
    owned.invoke(value);
    if (value != 1) return false;
    value = 0;
    borrowed.bind(callable);
    borrowed.invoke(value);
    return value == 1;
}
} // namespace

int main()
{
    const bool ok = check<DefaultedValue>() && check<EllipsisValue>()
        && check<VolatileValue>() && check<MutableReference>();
    std::printf("slot overload specialization: %s\n", ok ? "passed" : "failed");
    return ok ? 0 : 1;
}
