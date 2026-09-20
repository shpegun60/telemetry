// Explicit const/reference template arguments must not turn borrowed command
// owners or metadata into references to temporaries. All cases require rejection.
#include "Telemetry.h"
using namespace telemetry;

struct Owner {
    CommandResult apply(int) const noexcept { return CommandResult::Executed; }
} owner;
CommandResult apply(int) noexcept { return CommandResult::Executed; }
const Owner temporaryOwner() noexcept { return {}; }
using Args = decltype(commandArgs(arg("value", "", 1)));
constexpr auto args = commandArgs(arg("value", "", 1));
struct Callable {
    CommandResult operator()(int) const noexcept { return CommandResult::Executed; }
} callable;

#if TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 1
auto rejected = command<&Owner::apply, const Owner&>("owner", Owner{});
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 2
auto rejected = command<&Owner::apply, const Owner>("owner", Owner{}, arg<0>("value"));
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 3
auto rejected = command<&Owner::apply>("owner", temporaryOwner());
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 4
auto rejected = detail::materializeCommand<&Owner::apply, const Owner&>("owner", Owner{});
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 5
auto rejected = detail::materializeCommand<&Owner::apply, const Owner>("owner", Owner{}, args);
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 6
auto rejected = detail::materializeCommand<&apply, const Args&>("metadata", commandArgs(arg("value", "", 1)));
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 7
auto rejected = detail::materializeCommand<&apply, const Args>("metadata", commandArgs(arg("value", "", 1)));
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 8
auto rejected = detail::materializeCommand<&Owner::apply, Owner, const Args&>(
    "metadata", owner, commandArgs(arg("value", "", 1)));
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 9
auto rejected = detail::materializeCommand<Callable, const Args&>(
    "metadata", callable, commandArgs(arg("value", "", 1)));
#elif TELEMETRY_COMMAND_LIFETIME_FAIL_CASE == 10
auto rejected = detail::materializeCommand<Callable, const Args>(
    "metadata", callable, commandArgs(arg("value", "", 1)));
#else
#error "Select TELEMETRY_COMMAND_LIFETIME_FAIL_CASE from 1 through 10"
#endif

int main() {}
