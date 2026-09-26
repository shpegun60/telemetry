// Every public weak NTTP form must instantiate. WeakTargetCheck.cpp exercises
// absence after ELF linking; this file also compiles on non-ELF hosts.
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
[[maybe_unused]] constexpr FieldTable probe{field<&missingRead>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 2
[[maybe_unused]] constexpr FieldTable probe{field<&strongRead, &missingWrite>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 3
[[maybe_unused]] constexpr CommandTable probe{command<&missingCommand>("run")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 4
[[maybe_unused]] const FieldTable probe{field<&missingRead>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 5
[[maybe_unused]] const FieldTable probe{field<&strongRead, &missingWrite>("value", "")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 6
[[maybe_unused]] const CommandTable probe{command<&missingCommand>("run")};
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 7
[[maybe_unused]] const auto probe = Getter::bind<&missingRead>();
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 8
[[maybe_unused]] const auto probe = Setter::bind<&missingScalarWrite>();
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 9
[[maybe_unused]] const auto probe = Getter::bindContext<&missingContextRead>(value);
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 10
[[maybe_unused]] const auto probe = Setter::bindContext<&missingContextWrite>(value);
#elif TELEMETRY_WEAK_TARGET_FAIL_CASE == 11
void probe()
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
