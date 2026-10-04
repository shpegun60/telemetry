// Verify that a strong definition overrides a weak default used as a
// compile-time telemetry target across translation units (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
// Builds the default definition, strong replacement and caller as separate translation units.
// The linked control checks that a strong callback replaces an ELF weak default; other formats keep a build control.

#include <telemetry/Telemetry.hpp>
#include "SharedSupport.hpp"

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
extern "C" __attribute__((weak)) float overrideRead() noexcept
{
	return -1.f;
}

extern "C" __attribute__((weak)) telemetry::WriteResult overrideWrite(float) noexcept
{
	return telemetry::WriteResult::InvalidValue;
}

extern "C" __attribute__((weak)) telemetry::CommandResult overrideCommand() noexcept
{
	return telemetry::CommandResult::InvalidValue;
}

#elif defined(TELEMETRY_WEAK_OVERRIDE_STRONG)
extern "C" float overrideRead() noexcept
{
	return 42.f;
}

extern "C" telemetry::WriteResult overrideWrite(float) noexcept
{
	return telemetry::WriteResult::Applied;
}

extern "C" telemetry::CommandResult overrideCommand() noexcept
{
	return telemetry::CommandResult::Executed;
}

#else
constexpr telemetry::FieldTable fields{telemetry::field<&overrideRead, &overrideWrite>("Override")};
constexpr telemetry::CommandTable commands{telemetry::command<&overrideCommand>("Override")};

int main()
{
	CHECK(fields.read<0>() == 42.f);
	CHECK(fields.write<0>(1.f) == telemetry::WriteResult::Applied);
	CHECK(commands.call<0>() == telemetry::CommandResult::Executed);
	reportChecks();
	return 0;
}
#endif

#else
int main()
{
	return 0;
}
#endif
