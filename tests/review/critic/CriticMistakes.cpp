// Critic trial: natural first-week mistakes, one per CRITIC_CASE, to judge the diagnostics.
// Compile with -fsyntax-only -DCRITIC_CASE=N; see Notes.txt for the results.
#include "Telemetry.h"

#include <cstdint>

using namespace telemetry;

enum class Mode : std::uint8_t { Off, Auto, Manual };

struct Device {
    float limit() const noexcept { return limit_; }
    WriteResult setLimit(float v) noexcept { limit_ = v; return WriteResult::Applied; }
    WriteResult setLimitDouble(double v) noexcept { limit_ = static_cast<float>(v); return WriteResult::Applied; }
    WriteResult setLimitThrowing(float v) { limit_ = v; return WriteResult::Applied; }
    bool setLimitBool(float v) noexcept { limit_ = v; return true; }
    float voltageNoNoexcept() const { return 230.0f; }
    const float& limitRef() const noexcept { return limit_; }
    float limit_ = 1.0f;
};

Device device;

#if CRITIC_CASE == 1
// 1. Setter takes double while the getter returns float.
constexpr FieldTable t{field<&Device::limit, &Device::setLimitDouble>("Limit", "V", device)};
#elif CRITIC_CASE == 2
// 2. Setter is missing noexcept.
constexpr FieldTable t{field<&Device::limit, &Device::setLimitThrowing>("Limit", "V", device)};
#elif CRITIC_CASE == 3
// 3. Setter returns bool instead of WriteResult.
constexpr FieldTable t{field<&Device::limit, &Device::setLimitBool>("Limit", "V", device)};
#elif CRITIC_CASE == 4
// 4. Getter is missing noexcept.
constexpr FieldTable t{field<&Device::voltageNoNoexcept>("Ua", "V", device)};
#elif CRITIC_CASE == 5
// 5. Temporary owner.
const FieldTable t{field<&Device::limit>("Limit", "V", Device{})};
#elif CRITIC_CASE == 6
// 6. Duplicate field names, no optional check: does anything notice?
constexpr FieldTable t{field<&Device::limit>("Limit", "V", device), field<&Device::limit>("Limit", "V", device)};
constexpr FieldCatalogTable c{group("a", t), group("a", t)};
int use() { return c.index().read(makeId(1, 1)).type() == ScalarType::F32; }
#elif CRITIC_CASE == 7
// 7. Duplicate field names with the optional check the README recommends.
constexpr FieldTable t{field<&Device::limit>("Limit", "V", device), field<&Device::limit>("Limit", "V", device)};
static_assert(names_unique(t.data(), t.size()));
#elif CRITIC_CASE == 8
// 8. Limits written as double literals for a float field.
constexpr FieldTable t{field<&Device::limit, &Device::setLimit>("Limit", "V", device, limits(260.0, 100.0, 300.0))};
#elif CRITIC_CASE == 9
// 9. Limits in (min, max, default) order, as withLimits() takes them.
constexpr FieldTable t{field<&Device::limit, &Device::setLimit>("Limit", "V", device, limits(100.0f, 300.0f, 260.0f))};
#elif CRITIC_CASE == 10
// 10. Getter returns a const reference.
constexpr FieldTable t{field<&Device::limitRef>("Limit", "V", device)};
#elif CRITIC_CASE == 11
// 11. Owner is a local object and the table is constexpr.
int use()
{
    Device local;
    constexpr FieldTable t{field<&Device::limit>("Limit", "V", local)};
    return static_cast<int>(t.size());
}
#elif CRITIC_CASE == 12
// 12. Wrong position in a compile-time read.
constexpr FieldTable t{field<&Device::limit>("Limit", "V", device)};
auto v = t.read<3>();
#endif
