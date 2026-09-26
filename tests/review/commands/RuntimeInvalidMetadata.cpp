// Review probe (commands slice): README "runtime construction of an invalid
// definition terminates". Commands have no runner control for this; check that a
// non-constant CommandTable with invalid metadata aborts during construction.
// argv[1]: 1 = default outside [min,max], 2 = null name, 3 = NaN bound,
//          4 = enum limits wider than the named interval, 5 = enumSpec default outside.
#include "Telemetry.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace telemetry;

enum class Mode : std::uint8_t { A, B, C };
struct Dev {
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
    std::printf("returned normally\n");
    return 0;
}
