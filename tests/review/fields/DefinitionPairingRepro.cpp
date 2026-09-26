// Review repro (fields slice): FieldTableAccess reads Getter/Setter union
// members "with their original exact type", which is only true when the
// Field was produced by the factory that chose the Access type. The
// FieldDefinition constructor is public, so decltype(field(...)){anyField}
// pairs an arbitrary descriptor with a typed accessor.
#include "Telemetry.h"
#include <cstdio>

using namespace telemetry;

float readA() noexcept { return 1.0f; }
float readB() noexcept { return 2.0f; }
struct Owner {
    float value = 3.0f;
    float read() const noexcept { return value; }
};
Owner owner;

int main(int argc, char**)
{
    // Same row, two answers: the typed path calls readA (NTTP), the
    // descriptor calls readB.
    using FreeDefinition = decltype(field<&readA>("a", ""));
    const FieldTable mixed{FreeDefinition{Field{"b", "", ScalarType::F32, &readB}}};
    std::printf("typed read<0>() = %g, dynamic table[0].read<float>() = %g\n",
                mixed.read<0>().value_or(-1), mixed[0].read<float>().value_or(-1));
    if (argc > 1) {
        // The typed path reads Getter's union member 'object' while the active
        // member is the float function pointer: the function's address is
        // used as an Owner*. Undefined behaviour; shown only on request.
        using MemberDefinition = decltype(field<&Owner::read>("m", "", owner));
        const FieldTable punned{MemberDefinition{Field{"p", "", ScalarType::F32, &readB}}};
        std::printf("punned typed read = %g\n", punned.read<0>().value_or(-1));
    }
    return 0;
}
