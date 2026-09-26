// Review repro (slots): slotSignatureMatches checks a generic call operator as
// operator()<A...> with the slot's parameter types spelled explicitly. For a
// slot parameter `int&` and a lambda parameter `auto x`, operator()<int&> has
// signature void(int&) and passes, but the actual call deduces auto = int and
// takes a copy. The "exact parameter match" is therefore not what is invoked.
#include "Telemetry.h"
#include <cstdio>
using namespace telemetry;

int main()
{
    DelegateSlot<void(int&) noexcept> owned;
    owned.bind([](auto x) noexcept { x = 5; }); // accepted; x is an int copy at run time
    int value = 1;
    owned.invoke(value);

    DelegateRefSlot<void(int&) noexcept> borrowed;
    auto generic = [](auto x) noexcept { x = 7; };
    borrowed.bind(generic);
    int other = 1;
    borrowed.invoke(other);

    // A concrete by-value lambda with the same body is rejected (case 37/38 style):
    //   owned.bind([](int x) noexcept { x = 5; });  // "target signature must match exactly"
    std::printf("owned: value=%d (5 if written through the reference)\n", value);
    std::printf("borrowed: other=%d (7 if written through the reference)\n", other);
    return 0;
}
