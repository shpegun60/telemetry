// Reject wrong signatures and temporary slots before borrowing their addresses.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
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
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 6
auto bad = field("", "", Read{});
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 7
auto bad = field("", "", reader, Write{});
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 8
auto bad = command("", Run{});
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 9
auto bad = field<const Read>("", "", Read{});
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 10
auto bad = command<const Run>("", Run{});
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 11
auto bad = reader;
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 12
auto bad = std::move(reader);
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 13
void bad() { Read next; next = reader; }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 14
void bad() { Read next; next = std::move(reader); }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 15
FunctionSlot<WriteResult(double) noexcept> mismatch;
auto bad = field("", "", reader, mismatch);
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 16
CommandTable bad{command("", reader)};
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 17
FunctionSlot<float(float) noexcept> withArgument;
auto bad = field("", "", withArgument);
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 18
FunctionSlot<CommandResult(const float&) noexcept> withReference;
CommandTable bad{command("", withReference)};
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 19
volatile Read unstable;
auto bad = field("", "", unstable);
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 20
FunctionSlot<int(float) noexcept> wrongResult;
auto bad = field("", "", reader, wrongResult);
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 21
void bad() { reader(); }
#elif TELEMETRY_FUNCTION_SLOT_FAIL_CASE == 22
auto bad = field<const Read, const Write>("", "", Read{}, Write{});
#else
#error Select TELEMETRY_FUNCTION_SLOT_FAIL_CASE from 1 through 22
#endif
int main() {}
