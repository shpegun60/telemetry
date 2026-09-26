// Stable named initializer-list objects remain borrowable; temporary lists do not.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <initializer_list>
#include <cstdio>

using namespace telemetry;

namespace {
const std::initializer_list<int> stableNumbers{2, 3};
int readList(const std::initializer_list<int>& values) noexcept { return *values.begin(); }
WriteResult writeList(const std::initializer_list<int>& values, const Scalar& value) noexcept
{
    return *values.begin() == value.get<std::int32_t>()
        ? WriteResult::Applied : WriteResult::InvalidValue;
}
}

int main()
{
    const Getter getter = Getter::bindContext<&readList, const std::initializer_list<int>>(stableNumbers);
    const Setter setter = Setter::bindContext<&writeList, const std::initializer_list<int>>(stableNumbers);
    OwnerSlot<const std::initializer_list<int>> owner;
    owner.bind(stableNumbers);
    const bool ok = getter().get<std::int32_t>() == 2
        && setter(Scalar::fromS32(2)) == WriteResult::Applied
        && owner.get() == &stableNumbers;
    std::printf("named initializer-list binding: %s\n", ok ? "passed" : "failed");
    return ok ? 0 : 1;
}
