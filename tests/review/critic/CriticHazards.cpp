// Critic trial: what an adopter sees when names collide and when a firmware update reorders rows.
#include "Telemetry.h"
#include "serialization/TelemetryJson.h"

#include <cstdio>

using namespace telemetry;

struct Settings {
    float overVoltage = 260.0f;
    float underVoltage = 190.0f;
    float readOver() const noexcept { return overVoltage; }
    WriteResult writeOver(float v) noexcept { overVoltage = v; return WriteResult::Applied; }
    float readUnder() const noexcept { return underVoltage; }
    WriteResult writeUnder(float v) noexcept { underVoltage = v; return WriteResult::Applied; }
};
Settings settings;

// Firmware v1 and v2 differ only in row order, as after an innocent "sort alphabetically" edit.
constexpr FieldTable v1{
    field<&Settings::readOver, &Settings::writeOver>("OverVoltage", "V", settings, limits(260.0f, 100.0f, 300.0f)),
    field<&Settings::readUnder, &Settings::writeUnder>("UnderVoltage", "V", settings, limits(190.0f, 100.0f, 300.0f)),
};
constexpr FieldTable v2{
    field<&Settings::readUnder, &Settings::writeUnder>("UnderVoltage", "V", settings, limits(190.0f, 100.0f, 300.0f)),
    field<&Settings::readOver, &Settings::writeOver>("OverVoltage", "V", settings, limits(260.0f, 100.0f, 300.0f)),
};
constexpr FieldCatalogTable fw1{group("protection", v1)};
constexpr FieldCatalogTable fw2{group("protection", v2)};

// Two groups with the same name: accepted without any check.
constexpr FieldCatalogTable dup{group("protection", v1), group("protection", v2)};

char buffer[1024];

int main()
{
    // A host tool cached "OverVoltage = id 0" from firmware v1 and sends a new over-voltage trip level.
    const FieldId cachedId = makeId(0, 0);
    const auto r = fw2.index().write(cachedId, 280.0f);
    std::printf("stale id 0 write on v2 -> result %d, over=%g under=%g\n", static_cast<int>(r),
                static_cast<double>(settings.overVoltage), static_cast<double>(settings.underVoltage));
    std::printf("schema fingerprints: v1=%08lx v2=%08lx\n", static_cast<unsigned long>(schemaCrc(fw1.index())),
                static_cast<unsigned long>(schemaCrc(fw2.index())));
    writeValues(dup.index(), buffer, sizeof(buffer));
    std::printf("values JSON with duplicate group names: %s\n", buffer);
    return 0;
}
