// A constexpr field table must compile and run at the usual GCC Debug -Og level.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Checks that constexpr Field declarations remain usable with debug-oriented optimization.
// The table is evaluated at compile time so accidental optimization requirements become visible.

#include <telemetry/Telemetry.hpp>
#include "SharedSupport.hpp"
using namespace telemetry;
float value = 0;

float readValue() noexcept
{
	return value;
}

WriteResult writeValue(float next) noexcept
{
	value = next;
	return WriteResult::Applied;
}

constexpr FieldTable rows{field<&readValue, &writeValue>("x")};

int main()
{
	const bool written = rows.write<0>(1.0f) == WriteResult::Applied;
	CHECK(written);
	CHECK(rows.read<0>() == 1.0f);
	reportChecks();
	return 0;
}
