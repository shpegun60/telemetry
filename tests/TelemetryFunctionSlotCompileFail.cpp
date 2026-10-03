// Reject wrong signatures and temporary slots before borrowing their addresses.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include <telemetry/Telemetry.hpp>
using namespace telemetry;
using Read = FunctionSlot<float() noexcept>;
using Write = FunctionSlot<WriteResult(float) noexcept>;
using Run = FunctionSlot<CommandResult(float) noexcept>;
Read reader;
Write writer;
Run run;
#if TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 1
FunctionSlot<float()> bad;
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 2
FunctionSlot<float(*)() noexcept> bad;
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 3
void bad() { reader.bind([]() { return 1.f; }); }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 4
void bad() { reader.bind([]() noexcept { return 1.; }); }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 5
void bad() { float x = 1; reader.bind([x]() noexcept { return x; }); }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 11
auto bad = reader;
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 12
auto bad = std::move(reader);
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 13
void bad() { Read next; next = reader; }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 14
void bad() { Read next; next = std::move(reader); }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 21
void bad() { reader(); }

#else
#error "Select a maintained shared case"
#endif
int main() {}
