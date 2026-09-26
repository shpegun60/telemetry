// Review repro (slots): result ordering of the raw Field::set path versus slot
// state, JSON values of empty slots, and schema CRC across bind/reset.
// Build: g++ -std=c++17 -Ilib/telemetry -Ilib/delegate <this> + the three library sources.
#include "Telemetry.h"
#include "serialization/TelemetryJson.h"
#include <cstdio>
#include <cstring>
using namespace telemetry;

enum class Mode : std::uint16_t { Off, Auto, Manual };
struct Meter {
    float value = 1.f;
    Mode mode = Mode::Off;
    float read() const noexcept { return value; }
    WriteResult write(float v) noexcept { value = v; return WriteResult::Applied; }
    Mode readMode() const noexcept { return mode; }
    WriteResult writeMode(Mode m) noexcept { mode = m; return WriteResult::Applied; }
};
Meter meter;

OwnerSlot<Meter> owner;
FunctionSlot<float() noexcept> fnRead;
FunctionSlot<WriteResult(float) noexcept> fnWrite;
ContextFunctionSlot<float() noexcept> ctxRead;
ContextFunctionSlot<WriteResult(float) noexcept> ctxWrite;
DelegateRefSlot<float() noexcept> refRead;
DelegateRefSlot<WriteResult(float) noexcept> refWrite;
DelegateSlot<float() noexcept> ownRead;
DelegateSlot<WriteResult(float) noexcept> ownWrite;
FunctionSlot<Mode() noexcept> modeRead;
FunctionSlot<WriteResult(Mode) noexcept> modeWrite;

constexpr FieldTable rows{
    field<&Meter::read, &Meter::write>("Owner", "V", owner, limits(1.f, 0.f, 10.f)),
    field("Function", "V", fnRead, fnWrite, limits(1.f, 0.f, 10.f)),
    field("Context", "V", ctxRead, ctxWrite, limits(1.f, 0.f, 10.f)),
    field("Ref", "V", refRead, refWrite, limits(1.f, 0.f, 10.f)),
    field("Owned", "V", ownRead, ownWrite, limits(1.f, 0.f, 10.f)),
    field<&Meter::readMode, &Meter::writeMode>("OwnerMode", "", owner),
    field("FunctionMode", "", modeRead, modeWrite)};
constexpr FieldCatalogTable catalog{group("g", rows)};

const char* name(WriteResult r)
{
    switch (r) {
    case WriteResult::Applied: return "Applied";
    case WriteResult::NotFound: return "NotFound";
    case WriteResult::ReadOnly: return "ReadOnly";
    case WriteResult::InvalidValue: return "InvalidValue";
    case WriteResult::Busy: return "Busy";
    case WriteResult::Unavailable: return "Unavailable";
    }
    return "?";
}

float readMeter(void* p) noexcept { return static_cast<Meter*>(p)->read(); }
WriteResult writeMeter(void* p, float v) noexcept { return static_cast<Meter*>(p)->write(v); }
float freeRead() noexcept { return meter.read(); }
WriteResult freeWrite(float v) noexcept { return meter.write(v); }

void report(const char* phase)
{
    for (std::size_t i = 0; i < 5; ++i) {
        // Wrong Scalar alternative (U16 for an F32 field) through the raw setter,
        // then an out-of-limits value through the checked write path.
        std::printf("%-6s %-9s raw set(U16)=%-12s write(11)=%-12s\n", phase, rows[i].name,
                    name(rows[i].set(Scalar::fromU16(3))), name(catalog.write(makeId(0, std::uint16_t(i)), 11)));
    }
    for (std::size_t i = 5; i < 7; ++i) {
        // Right alternative, code 7 outside the enum dictionary [0, 2].
        std::printf("%-6s %-12s raw set(U16 7)=%-12s write(7)=%-12s\n", phase, rows[i].name,
                    name(rows[i].set(Scalar::fromU16(7))), name(catalog.write(makeId(0, std::uint16_t(i)), 7)));
    }
}

int main()
{
    char before[2048], values[2048];
    const auto crcEmpty = schemaCrc(catalog.index());
    writeSchema(catalog.index(), before, sizeof before);
    writeValues(catalog.index(), values, sizeof values);
    std::printf("empty values: %s\n", values);
    report("empty");

    owner.bind(meter);
    fnRead.bind(&freeRead); fnWrite.bind(&freeWrite);
    ctxRead.bind(&readMeter, &meter); ctxWrite.bind(&writeMeter, &meter);
    refRead.bind<&Meter::read>(meter); refWrite.bind<&Meter::write>(meter);
    ownRead.bind([]() noexcept { return meter.read(); });
    ownWrite.bind([](float v) noexcept { return meter.write(v); });
    modeRead.bind([]() noexcept { return meter.mode; });
    modeWrite.bind([](Mode m) noexcept { meter.mode = m; return WriteResult::Applied; });
    report("bound");
    writeValues(catalog.index(), values, sizeof values);
    std::printf("bound values: %s\n", values);
    char after[2048];
    writeSchema(catalog.index(), after, sizeof after);
    std::printf("schema CRC stable across bind: %s (0x%08lx)\n",
                crcEmpty == schemaCrc(catalog.index()) && std::strcmp(before, after) == 0 ? "yes" : "NO",
                static_cast<unsigned long>(crcEmpty));
    return 0;
}
