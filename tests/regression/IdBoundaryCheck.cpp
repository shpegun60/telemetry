// Packed IDs, local enum positions, and checked decomposition boundaries.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <atomic>
#include <cstdio>
#include <limits>

#ifndef TELEMETRY_ID_BOUNDARY_ABORT_CASE
#define TELEMETRY_ID_BOUNDARY_ABORT_CASE 0
#endif

using namespace telemetry;
namespace {
enum class Position : std::uint64_t { First, Second, TooWide = 0x100000001ULL };
enum class Group : unsigned { Meter, Sensor };
float meterValue = 230.f;
float sensorValue = 21.5f;
unsigned meterCalls = 0;
unsigned sensorCalls = 0;
float meterRead() noexcept { return meterValue; }
float sensorRead() noexcept { return sensorValue; }
WriteResult sensorWrite(float value) noexcept { sensorValue = value; return WriteResult::Applied; }
CommandResult meterRun() noexcept { ++meterCalls; return CommandResult::Executed; }
CommandResult sensorRun() noexcept { ++sensorCalls; return CommandResult::Executed; }
constexpr FieldTable meterFields{field<&meterRead>("voltage", "V")};
constexpr FieldTable sensorFields{field<&sensorRead, &sensorWrite>("temperature", "C")};
constexpr FieldCatalogTable fields{group("meter", meterFields), group("sensor", sensorFields)};
constexpr Catalog raw[]{Catalog{"meter", meterFields.data(), meterFields.size()},
                        Catalog{"sensor", sensorFields.data(), sensorFields.size()}};
[[maybe_unused]] constexpr auto fixed = CatalogIndex::bind<raw>();
constexpr CommandTable meter{command<&meterRun>("reset")};
constexpr CommandTable sensor{command<&sensorRun>("calibrate")};
[[maybe_unused]] constexpr CommandCatalogTable commands{group("meter", meter), group("sensor", sensor)};
struct WireId {
    std::uint64_t value;
    constexpr operator std::uint64_t() const noexcept { return value; }
};

static_assert(makeId<Group::Sensor, Position::First>() == makeId(1, 0));
static_assert(makeId<65535, 65535>() == std::numeric_limits<PackedId>::max());
static_assert(groupOf(std::uint64_t{0xffffffffu}) == 65535);
static_assert(indexOf(std::uint64_t{0xffffffffu}) == 65535);
static_assert(tryGroupOf(0x12345678ULL) == 0x1234);
static_assert(tryIndexOf(0x12345678ULL) == 0x5678);
static_assert(!tryGroupOf(0x100020001ULL) && !tryIndexOf(0x100020001ULL));
static_assert(!tryGroupOf(-1) && !tryIndexOf(-1));
static_assert(!tryMakeId(65536, 0) && !tryMakeId(0, -1));
static_assert(fields.index().catalog(Group::Sensor) == fields.data() + 1);

#if TELEMETRY_ID_BOUNDARY_ABORT_CASE == 6
[[maybe_unused]] const PackedId invalidBeforeMain = makeId(0, 70000);
#endif
}

int main()
{
#if TELEMETRY_ID_BOUNDARY_ABORT_CASE == 1
    volatile std::uint64_t id = 0x100020001ULL;
    return groupOf(id);
#elif TELEMETRY_ID_BOUNDARY_ABORT_CASE == 2
    volatile std::uint64_t id = 0x100020001ULL;
    return indexOf(id);
#elif TELEMETRY_ID_BOUNDARY_ABORT_CASE == 3
    volatile int id = -1;
    return groupOf(id);
#elif TELEMETRY_ID_BOUNDARY_ABORT_CASE == 4
    volatile int id = -1;
    return indexOf(id);
#elif TELEMETRY_ID_BOUNDARY_ABORT_CASE == 5
    return static_cast<int>(makeId(0, 65537));
#elif TELEMETRY_ID_BOUNDARY_ABORT_CASE == 0 || TELEMETRY_ID_BOUNDARY_ABORT_CASE == 6
    bool ok = true;
    unsigned checks = 0;
    const auto check = [&](bool result, int line) noexcept {
        ++checks;
        if (!result) std::printf("ID boundary check failed at line %d\n", line);
        ok = ok && result;
    };
#define CHECK(...) check(bool((__VA_ARGS__)), __LINE__)
    CHECK(sensorFields.read<Position::First>() == 21.5f);
    CHECK(sensorFields.write<Position::First>(22.f) == WriteResult::Applied);
    CHECK(sensor.call<Position::First>() == CommandResult::Executed);
    CHECK(sensor.call(Position::First) == CommandResult::Executed);
    CHECK(sensor.index().find(Position::First) == sensor.data());
    CHECK(sensor.index().execute(Position::First, nullptr, 0) == CommandResult::Executed);
    CHECK(sensor.index().call(Position::First) == CommandResult::Executed);
    CHECK(sensor.call(Position::TooWide) == CommandResult::NotFound);
    CHECK(sensor.index().find(Position::TooWide) == nullptr);
    CHECK(meterCalls == 0 && sensorCalls == 4);

    const PackedId sensorId = makeId<Group::Sensor, Position::First>();
    CHECK(fields.read<float>(sensorId) == 22.f);
    CHECK(fixed.read<float>(sensorId) == 22.f);
    CHECK(commands.call(sensorId) == CommandResult::Executed);
    CHECK(fields.read<makeId<1, 0>()>() == 22.f);
    CHECK(commands.call<makeId<1, 0>()>() == CommandResult::Executed);
    CHECK(fields.read<UINT64_C(65536), float>() == 22.f);
    CHECK(fixed.read<INT64_C(65536), float>() == 22.f);
    CHECK(fields.write<UINT64_C(65536)>(22.f) == WriteResult::Applied);
    CHECK(fixed.write<INT64_C(65536)>(22.f) == WriteResult::Applied);
    CHECK(groupOf(sensorId) == 1 && indexOf(sensorId) == 0);
    CHECK(groupOf(std::int64_t{sensorId}) == 1 && indexOf(std::int64_t{sensorId}) == 0);
    CHECK(tryGroupOf(0xffffffffULL) == 65535 && tryIndexOf(0xffffffffULL) == 65535);
    CHECK(tryMakeId(Group::Sensor, Position::First) == sensorId);

    const auto reject = [&](auto id) noexcept {
        CHECK(fields.index().find(id) == nullptr);
        CHECK(fields.find(id) == nullptr);
        CHECK(fixed.find(id) == nullptr);
        CHECK(fields.index().read(id).type() == ScalarType::Null);
        CHECK(fields.read(id).type() == ScalarType::Null);
        CHECK(fixed.read(id).type() == ScalarType::Null);
        CHECK(!fields.index().template read<float>(id));
        CHECK(!fields.template read<float>(id));
        CHECK(!fixed.template read<float>(id));
        CHECK(fields.index().write(id, 23.f) == WriteResult::NotFound);
        CHECK(fields.write(id, 23.f) == WriteResult::NotFound);
        CHECK(fixed.write(id, 23.f) == WriteResult::NotFound);
        CHECK(commands.index().find(id) == nullptr);
        CHECK(commands.find(id) == nullptr);
        CHECK(commands.index().execute(id, nullptr, 0) == CommandResult::NotFound);
        CHECK(commands.execute(id, nullptr, 0) == CommandResult::NotFound);
        CHECK(commands.index().call(id) == CommandResult::NotFound);
        CHECK(commands.call(id) == CommandResult::NotFound);
        CHECK(sensor.index().find(id) == nullptr);
        CHECK(sensor.index().execute(id, nullptr, 0) == CommandResult::NotFound);
        CHECK(sensor.index().call(id) == CommandResult::NotFound);
        CHECK(sensor.call(id) == CommandResult::NotFound);
        CHECK(!tryGroupOf(id) && !tryIndexOf(id));
    };
    volatile std::uint64_t received = 0x100000000ULL | sensorId;
    reject(std::uint64_t{received});
    reject(std::int64_t{-1});
    std::atomic<std::uint64_t> atomicWide{received};
    reject(atomicWide.load());
    const WireId wire{received};
    reject(static_cast<std::uint64_t>(wire));
    std::atomic<FieldId> atomicNarrow{sensorId};
    CHECK(fields.read<float>(atomicNarrow.load()) == 22.f);
    CHECK(commands.call(atomicNarrow.load()) == CommandResult::Executed);
    CHECK(fields.index().catalog(std::uint64_t{65536}) == nullptr);
    CHECK(fixed.catalog(std::uint64_t{65536}) == nullptr);
    CHECK(commands.index().catalog(std::uint64_t{65536}) == nullptr);
    CHECK(fields.index().catalog(-1) == nullptr && commands.index().catalog(-1) == nullptr);
    CHECK(sensorValue == 22.f && meterCalls == 0 && sensorCalls == 7);
#undef CHECK
    std::printf("%s %u ID boundary checks passed\n", ok ? "All" : "Not all", checks);
    return ok ? 0 : 1;
#else
#error "Unknown TELEMETRY_ID_BOUNDARY_ABORT_CASE"
#endif
}
