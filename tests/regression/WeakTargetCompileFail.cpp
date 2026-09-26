// Public NTTP callbacks need a constant nonnull address. A GNU weak declaration
// can resolve to null even though it names a function. Runtime nullable pointer
// forms are covered separately by NullChecksFlag.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"

#ifndef TELEMETRY_WEAK_TARGET_FAIL_CASE
#define TELEMETRY_WEAK_TARGET_FAIL_CASE 0
#endif

using namespace telemetry;

extern "C" float missingRead() noexcept __attribute__((weak));
extern "C" WriteResult missingWrite(float) noexcept __attribute__((weak));
extern "C" CommandResult missingCommand() noexcept __attribute__((weak));
extern "C" WriteResult missingScalarWrite(const Scalar&) noexcept __attribute__((weak));
extern "C" float missingContextRead(const float&) noexcept __attribute__((weak));
extern "C" WriteResult missingContextWrite(float&, const Scalar&) noexcept __attribute__((weak));

float value = 1.f;
float strongRead() noexcept { return value; }
WriteResult strongWrite(float next) noexcept { value = next; return WriteResult::Applied; }
CommandResult strongRun() noexcept { return CommandResult::Executed; }

#if TELEMETRY_WEAK_TARGET_FAIL_CASE == 1
constexpr FieldTable rejected{field<&missingRead>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 2
constexpr FieldTable rejected{field<&strongRead, &missingWrite>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 3
constexpr CommandTable rejected{command<&missingCommand>("run")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 4
const FieldTable rejected{field<&missingRead>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 5
const FieldTable rejected{field<&strongRead, &missingWrite>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 6
const CommandTable rejected{command<&missingCommand>("run")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 7
const auto rejected = Getter::bind<&missingRead>();
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 8
const auto rejected = Setter::bind<&missingScalarWrite>();
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 9
const auto rejected = Getter::bindContext<&missingContextRead>(value);
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 10
const auto rejected = Setter::bindContext<&missingContextWrite>(value);
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 11
void rejected()
{
    DelegateRefSlot<float() noexcept> slot;
    slot.bind<&missingRead>();
}
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE != 0
#error "Unknown TELEMETRY_WEAK_TARGET_FAIL_CASE"
#endif

int main()
{
    constexpr FieldTable fields{field<&strongRead, &strongWrite>("value", "")};
    constexpr CommandTable commands{command<&strongRun>("run")};
    return fields.read<0>() == 1.f
        && fields.write<0>(2.f) == WriteResult::Applied
        && fields.read<0>() == 2.f
        && commands.call<0>() == CommandResult::Executed ? 0 : 1;
}
