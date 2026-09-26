// Review repro (fields slice): a temporary whose conversion operator returns a
// reference into itself passes the "temporary owners are rejected" guard when
// the owner/closure type is given explicitly. The Field then points at the
// destroyed temporary. Build under ASan with -fsanitize-address-use-after-scope.
#include "Telemetry.h"
#include <cstdio>

using namespace telemetry;

struct Owner {
    int value = 1234;
    int read() const noexcept { return value; }
};
struct Holder {                      // e.g. a proxy/guard that owns its target
    Owner inner;
    operator const Owner&() const noexcept { return inner; }
    ~Holder() { inner.value = -777; }  // makes the end of its lifetime visible
};
struct Read {
    int value = 4321;
    int operator()() const noexcept { return value; }
};
struct ReadHolder {
    Read inner;
    operator const Read&() const noexcept { return inner; }
    ~ReadHolder() { inner.value = -888; }
};

__attribute__((noinline)) int readOwnerField()
{
    // Accepted by GCC 13/15 and Clang 18 in C++17 and C++20.
    const FieldTable table{field<&Owner::read, nullptr, const Owner>("x", "", Holder{})};
    return table.read<0>().value_or(-1);   // Holder{} died at the end of the declaration.
}

__attribute__((noinline)) int readClosureField()
{
    const FieldTable table{field<const Read>("y", "", ReadHolder{})};
    return table.read<0>().value_or(-1);
}

__attribute__((noinline)) int readGetter()
{
    const Getter getter = Getter::bind<&Owner::read, const Owner>(Holder{});
    return getter().get<std::int32_t>();
}

int main(int argc, char**)
{
    int result = 0;
    if (argc == 1) result = readOwnerField();
    else if (argc == 2) result = readClosureField();
    else result = readGetter();
    std::printf("value read through the binding: %d\n", result);
    return 0;
}
