// Review repro (slots): drives the two differently configured units against the
// same inline slots and table; prints what each unit observes.
#include "MacroOdrShared.h"
#include <cstdio>

int main()
{
    std::printf("empty: a=%g b=%g\n", odr_read_a(), odr_read_b());
    odr_bind_a();
    std::printf("bound by a: a=%g b=%g write a=%d b=%d store=%g\n", odr_read_a(), odr_read_b(),
                static_cast<int>(odr_write_a(5.f)), static_cast<int>(odr_write_b(6.f)), odrStore);
    odrOwnedRead.reset(); odrOwnedWrite.reset(); odrRefRead.reset();
    odr_bind_b();
    std::printf("bound by b: a=%g b=%g write a=%d b=%d store=%g\n", odr_read_a(), odr_read_b(),
                static_cast<int>(odr_write_a(8.f)), static_cast<int>(odr_write_b(9.f)), odrStore);
    return 0;
}
