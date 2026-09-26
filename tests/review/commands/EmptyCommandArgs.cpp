// Review probe (commands slice): an empty commandArgs() pack is both
// CommandArgs<>::positional and ::indexed (vacuous folds). Which rule applies
// when it decorates a command that has parameters?
// -DCASE=1: two-parameter target with commandArgs()   (expected: "no metadata")
// -DCASE=2: zero-parameter target with commandArgs()  (expected: accepted)
#include "Telemetry.h"
using namespace telemetry;

CommandResult two(float, int) noexcept { return CommandResult::Executed; }
CommandResult none() noexcept { return CommandResult::Executed; }
static_assert(CommandArgs<>::positional && CommandArgs<>::indexed);

#if CASE == 1
constexpr CommandTable rows{command<&two>("two", commandArgs())};
#elif CASE == 2
constexpr CommandTable rows{command<&none>("none", commandArgs())};
static_assert(rows[0].metadata != nullptr && rows[0].parameterCount() == 0);
#endif
int main() {}
