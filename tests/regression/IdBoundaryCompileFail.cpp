// Invalid ID categories must fail before any implicit narrowing conversion.
// CASE0 verifies local enum positions and explicit integral extraction.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <atomic>

#ifndef TELEMETRY_ID_BOUNDARY_FAIL_CASE
#define TELEMETRY_ID_BOUNDARY_FAIL_CASE 0
#endif

using namespace telemetry;
namespace id_boundary_rejection {
enum class Position : std::uint64_t { First, TooWide = 0x100000001ULL };
enum LegacyPosition { LegacyFirst };
float readValue() noexcept { return 1.f; }
WriteResult writeValue(float) noexcept { return WriteResult::Applied; }
CommandResult run() noexcept { return CommandResult::Executed; }
constexpr FieldTable rows{field<&readValue, &writeValue>("value", "")};
constexpr FieldCatalogTable fields{group("fields", rows)};
constexpr Catalog raw[]{Catalog{"fields", rows.data(), rows.size()}};
constexpr auto fixed = CatalogIndex::bind<raw>();
constexpr CommandTable local{command<&run>("run")};
constexpr CommandCatalogTable commands{group("commands", local)};
struct WireId {
    std::uint64_t value;
    constexpr operator std::uint64_t() const noexcept { return value; }
};
struct NarrowId {
    FieldId value;
    constexpr operator FieldId() const noexcept { return value; }
};
constexpr WireId wire{0x100000000ULL};
constexpr NarrowId narrow{0};
std::atomic<std::uint64_t> atomicWide{wire.value};
std::atomic<FieldId> atomicNarrow{0};
}
using namespace id_boundary_rejection;

int main()
{
#if TELEMETRY_ID_BOUNDARY_FAIL_CASE == 1
    (void)fields.index().find(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 2
    (void)fields.index().read(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 3
    (void)fields.index().read<float>(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 4
    (void)fields.index().write(Position::First, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 5
    (void)fields.find(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 6
    (void)fields.read(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 7
    (void)fields.read<float>(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 8
    (void)fields.write(Position::First, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 9
    (void)fixed.find(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 10
    (void)fixed.read(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 11
    (void)fixed.read<float>(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 12
    (void)fixed.write(Position::First, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 13
    (void)commands.index().find(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 14
    (void)commands.index().execute(Position::First, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 15
    (void)commands.index().call(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 16
    (void)commands.find(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 17
    (void)commands.execute(Position::First, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 18
    (void)commands.call(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 19
    (void)groupOf(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 20
    (void)indexOf(Position::First);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 21
    (void)fields.index().find(LegacyFirst);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 22
    (void)commands.index().execute(LegacyFirst, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 23
    (void)fields.index().find(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 24
    (void)fields.index().read(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 25
    (void)fields.index().read<float>(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 26
    (void)fields.index().write(wire, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 27
    (void)fields.index().catalog(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 28
    (void)fields.find(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 29
    (void)fields.read(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 30
    (void)fields.read<float>(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 31
    (void)fields.write(wire, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 32
    (void)fixed.find(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 33
    (void)fixed.read(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 34
    (void)fixed.read<float>(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 35
    (void)fixed.write(wire, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 36
    (void)fixed.catalog(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 37
    (void)commands.index().find(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 38
    (void)commands.index().execute(wire, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 39
    (void)commands.index().call(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 40
    (void)commands.index().catalog(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 41
    (void)commands.find(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 42
    (void)commands.execute(wire, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 43
    (void)commands.call(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 44
    (void)local.index().find(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 45
    (void)local.index().execute(wire, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 46
    (void)local.index().call(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 47
    (void)local.call(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 48
    (void)groupOf(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 49
    (void)indexOf(wire);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 50
    (void)makeId(wire, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 51
    (void)fields.index().find(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 52
    (void)fields.index().read(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 53
    (void)fields.index().read<float>(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 54
    (void)fields.index().write(atomicWide, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 55
    (void)fields.find(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 56
    (void)fixed.find(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 57
    (void)commands.index().execute(atomicWide, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 58
    (void)local.index().execute(atomicWide, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 59
    (void)local.call(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 60
    (void)groupOf(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 61
    (void)indexOf(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 62
    (void)fields.index().catalog(atomicWide);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 63
    (void)fields.index().find(0.0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 64
    (void)local.call(0.0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 65
    constexpr auto invalid = groupOf(0x100020001ULL); (void)invalid;
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 66
    constexpr auto invalid = indexOf(0x100020001ULL); (void)invalid;
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 67
    constexpr auto invalid = groupOf(-1); (void)invalid;
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 68
    constexpr auto invalid = indexOf(-1); (void)invalid;
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 69
    (void)makeId<-1, 0>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 70
    (void)makeId<0, 65536>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 71
    (void)makeId<65536, 0>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 72
    (void)makeId<0, Position::TooWide>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 73
    constexpr auto invalid = makeId(0, 65536); (void)invalid;
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 74
    constexpr auto invalid = makeId(-1, 0); (void)invalid;
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 75
    (void)makeId(0.0, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 76
    (void)tryMakeId(wire, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 77
    (void)fields.index().find(atomicNarrow);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 78
    (void)fields.index().read<float>(atomicNarrow);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 79
    (void)fields.index().write(atomicNarrow, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 80
    (void)commands.execute(atomicNarrow, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 81
    (void)fields.index().find(narrow);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 82
    (void)fields.index().read<float>(narrow);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 83
    (void)fields.index().write(narrow, 2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 84
    (void)commands.execute(narrow, nullptr, 0);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 85
    (void)fields.read<Position::First>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 86
    (void)fields.read<Position::First, float>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 87
    (void)fields.write<Position::First>(2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 88
    (void)fixed.read<Position::First>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 89
    (void)fixed.read<Position::First, float>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 90
    (void)fixed.write<Position::First>(2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 91
    (void)commands.call<Position::First>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 92
    (void)fields.read<LegacyFirst>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 93
    (void)fields.read<LegacyFirst, float>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 94
    (void)fields.write<LegacyFirst>(2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 95
    (void)fixed.read<LegacyFirst>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 96
    (void)fixed.read<LegacyFirst, float>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 97
    (void)fixed.write<LegacyFirst>(2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 98
    (void)commands.call<LegacyFirst>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 99
    (void)fields.read<0x100000000ULL>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 100
    (void)fixed.read<-1>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 101
    (void)commands.call<0x100000000ULL>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 102
    (void)fields.read<-1, float>();
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 103
    (void)fields.write<0x100000000ULL>(2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE == 104
    (void)fixed.write<0x100000000ULL>(2.f);
#elif TELEMETRY_ID_BOUNDARY_FAIL_CASE != 0
#error "Unknown TELEMETRY_ID_BOUNDARY_FAIL_CASE"
#endif
    return rows.read<Position::First>() == 1.f
        && local.call<Position::First>() == CommandResult::Executed
        && local.call(Position::First) == CommandResult::Executed
        && fields.find(atomicNarrow.load()) == rows.data()
        && fixed.find(atomicNarrow.load()) == rows.data()
        && commands.call(atomicNarrow.load()) == CommandResult::Executed
        && fields.find(static_cast<FieldId>(narrow)) == rows.data()
        && fields.find(static_cast<std::uint64_t>(wire)) == nullptr ? 0 : 1;
}
