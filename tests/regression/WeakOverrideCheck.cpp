// Verify that a strong definition overrides a weak default used as a
// compile-time telemetry target across translation units (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include "Telemetry.h"

#if defined(__ELF__) && (defined(__GNUC__) || defined(__clang__))

#if defined(TELEMETRY_WEAK_OVERRIDE_STRONG)
extern "C" float overrideRead() noexcept;
extern "C" telemetry::WriteResult overrideWrite(float) noexcept;
extern "C" telemetry::CommandResult overrideCommand() noexcept;
#else
extern "C" float overrideRead() noexcept __attribute__((weak));
extern "C" telemetry::WriteResult overrideWrite(float) noexcept __attribute__((weak));
extern "C" telemetry::CommandResult overrideCommand() noexcept __attribute__((weak));
#endif

#if defined(TELEMETRY_WEAK_OVERRIDE_DEFAULT)
extern "C" __attribute__((weak)) float overrideRead() noexcept { return -1.f; }
extern "C" __attribute__((weak)) telemetry::WriteResult overrideWrite(float) noexcept
{ return telemetry::WriteResult::InvalidValue; }
extern "C" __attribute__((weak)) telemetry::CommandResult overrideCommand() noexcept
{ return telemetry::CommandResult::InvalidValue; }

#elif defined(TELEMETRY_WEAK_OVERRIDE_STRONG)
extern "C" float overrideRead() noexcept { return 42.f; }
extern "C" telemetry::WriteResult overrideWrite(float) noexcept
{ return telemetry::WriteResult::Applied; }
extern "C" telemetry::CommandResult overrideCommand() noexcept
{ return telemetry::CommandResult::Executed; }

#else
constexpr telemetry::FieldTable fields{
    telemetry::field<&overrideRead, &overrideWrite>("Override", "")};
constexpr telemetry::CommandTable commands{
    telemetry::command<&overrideCommand>("Override")};

int main()
{
    return fields.read<0>() == 42.f
        && fields.write<0>(1.f) == telemetry::WriteResult::Applied
        && commands.call<0>() == telemetry::CommandResult::Executed ? 0 : 1;
}
#endif

#else
int main() { return 0; }
#endif
