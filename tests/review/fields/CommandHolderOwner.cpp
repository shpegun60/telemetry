#include "Telemetry.h"
using namespace telemetry;
struct Owner { int v = 1; CommandResult run(float) const noexcept { return v == 1 ? CommandResult::Executed : CommandResult::Failed; } };
struct Holder { Owner inner; operator const Owner&() const noexcept { return inner; } };
const CommandTable table{command<&Owner::run, const Owner>("run", Holder{})};
int main() { return table.call<0>(1.0f) == CommandResult::Executed ? 0 : 1; }
