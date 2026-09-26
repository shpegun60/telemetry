// A constexpr field table must compile and run at the usual GCC Debug -Og level.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
float value = 0;
float readValue() noexcept { return value; }
WriteResult writeValue(float next) noexcept { value = next; return WriteResult::Applied; }
constexpr FieldTable rows{field<&readValue, &writeValue>("x", "")};
int main()
{
    const bool written = rows.data()[0].write(1.0f) == WriteResult::Applied;
    return written && rows.data()[0].read<float>() == 1.0f ? 0 : 1;
}
