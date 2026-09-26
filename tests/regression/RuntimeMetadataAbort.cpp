// Regression promoted from tests/review/commands/RuntimeInvalidMetadata.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review probe (commands slice): README "runtime construction of an invalid
// definition terminates". Commands have no runner control for this; check that a
// non-constant CommandTable with invalid metadata aborts during construction.
// argv[1]: 1 = default outside [min,max], 2 = null parameter name, 3 = NaN bound,
//          4 = enum limits wider than the named interval, 5 = enumSpec default outside.
//          6 = null command name (through a runtime pointer).
//          7 = wide runtime makeId component (tryMakeId is the fallible API).
//          8..12 = missing mandatory field/unit/group labels in factories.
#include "Telemetry.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace telemetry;

enum class Mode : std::uint8_t { A, B, C };
struct Dev {
    float value() const noexcept { return 1.f; }
    CommandResult f(float) noexcept { return CommandResult::Executed; }
    CommandResult m(Mode) noexcept { return CommandResult::Executed; }
};

int main(int argc, char** argv)
{
    Dev dev; // Local owner: the table below cannot be a constant expression.
    const int which = argc > 1 ? std::atoi(argv[1]) : 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("constructing case %d\n", which);
    if (which == 1) { const CommandTable t{command<&Dev::f>("x", dev, arg<0>("v", "", 5.0f, 0.0f, 1.0f))}; std::printf("constructed %zu\n", t.size()); }
    if (which == 2) { const CommandTable t{command<&Dev::f>("x", dev, arg<0>(nullptr))}; std::printf("constructed %zu\n", t.size()); }
    if (which == 3) { const CommandTable t{command<&Dev::f>("x", dev, arg<0>("v", "", 0.0f, nan, 1.0f))}; std::printf("constructed %zu\n", t.size()); }
    if (which == 4) { const CommandTable t{command<&Dev::m>("x", dev, arg<0>("m", "", Mode::A, Mode::A, static_cast<Mode>(7)))}; std::printf("constructed %zu\n", t.size()); }
    if (which == 5) { const CommandTable t{command<&Dev::m>("x", dev, arg<0>("m", "", enumSpec<Mode::A, Mode::B>(Mode::C)))}; std::printf("constructed %zu\n", t.size()); }
    if (which == 6) { const char* name = nullptr; const CommandTable t{command<&Dev::f>(name, dev)}; std::printf("constructed %zu\n", t.size()); }
    if (which == 7) { volatile std::uint64_t group = UINT64_C(1) << 32; std::printf("id %u\n", makeId(group, 0)); }
    if (which == 8) { const FieldTable t{field<&Dev::value>(nullptr, "", dev)}; std::printf("constructed %zu\n", t.size()); }
    if (which == 9) { const FieldTable t{field<&Dev::value>("value", nullptr, dev)}; std::printf("constructed %zu\n", t.size()); }
    if (which == 10) { const FieldTable t{field(Field{nullptr, "", ScalarType::F32})}; std::printf("constructed %zu\n", t.size()); }
    if (which == 11) { const FieldTable rows{field<&Dev::value>("value", "", dev)}; const FieldCatalogTable t{group(nullptr, rows)}; std::printf("constructed %zu\n", t.size()); }
    if (which == 12) { const CommandTable rows{command<&Dev::f>("apply", dev)}; const CommandCatalogTable t{group(nullptr, rows)}; std::printf("constructed %zu\n", t.size()); }
    std::printf("returned normally\n");
    return 0;
}
