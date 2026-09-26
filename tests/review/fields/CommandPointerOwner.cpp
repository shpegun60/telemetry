#include "Telemetry.h"
using namespace telemetry;
struct Device { CommandResult run(float) noexcept { return CommandResult::Executed; } };
Device device; Device* pointer = &device;
constexpr CommandTable commands{command<&Device::run>("run", pointer)};
int main() { return commands.call<0>(1.0f) == CommandResult::Executed ? 0 : 1; }
