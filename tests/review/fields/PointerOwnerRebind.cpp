// Review probe (fields slice): a pointer-variable "owner" behaves like an
// unchecked OwnerSlot: re-seating the pointer silently redirects the row.
#include "Telemetry.h"
#include <cstdio>
using namespace telemetry;
struct Device {
    float volts;
    float voltage() const noexcept { return volts; }
    WriteResult setVoltage(float v) noexcept { volts = v; return WriteResult::Applied; }
};
Device a{1.0f}, b{2.0f};
Device* current = &a;
constexpr FieldTable table{field<&Device::voltage, &Device::setVoltage>("Ua", "V", current)};
int main()
{
    const auto first = table.read<0>().value_or(-1);
    current = &b;
    const auto second = table.read<0>().value_or(-1);
    const auto written = table.write<0>(5.0f);
    std::printf("read via pointer owner: %g then %g; write=%d a=%g b=%g\n",
                first, second, int(written), a.volts, b.volts);
}
