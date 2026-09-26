#include "field/TelemetryFieldTable.h"
#include "command/TelemetryCommandTable.h"
#include "slot/TelemetryOwnerSlot.h"
#include "slot/TelemetryFunctionSlot.h"
#include "slot/TelemetryContextFunctionSlot.h"
using namespace telemetry;
ContextFunctionSlot<float() noexcept> c; FunctionSlot<WriteResult(float) noexcept> w; FunctionSlot<CommandResult(float) noexcept> r;
constexpr FieldTable t{field("C","",c,w)}; constexpr CommandTable k{command("R", r)};
int main(){ return t.read<0>() ? 1 : int(k.call<0>(1.f)); }
