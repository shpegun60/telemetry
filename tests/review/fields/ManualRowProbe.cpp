// Review probe (fields slice): manual Field rows.
// 1. A native typed Setter whose C++ type does not map to declaredType makes
//    every Field::write return InvalidValue without calling the setter, even
//    for values that convert and pass the interval check. The mapping of
//    long/unsigned long/size_t differs between LP64, LLP64 and ARM32.
// 2. Persistent is accepted for a row whose declared type can never be read
//    or written (Null / unknown tag).
#include "Telemetry.h"
#include <cstddef>
#include <cstdio>

using namespace telemetry;

namespace {
int calls = 0;
float floatValue = 1.5f;
float readFloat() noexcept { return floatValue; }
WriteResult writeFloat(float value) noexcept { ++calls; floatValue = value; return WriteResult::Applied; }
unsigned long ulValue = 7;
unsigned long readUl() noexcept { return ulValue; }
WriteResult writeUl(unsigned long value) noexcept { ++calls; ulValue = value; return WriteResult::Applied; }
std::size_t sizeValue = 7;
std::size_t readSize() noexcept { return sizeValue; }
WriteResult writeSize(std::size_t value) noexcept { ++calls; sizeValue = value; return WriteResult::Applied; }
Scalar readScalar() noexcept { return Scalar::fromF32(1.0f); }
WriteResult writeScalar(const Scalar&) noexcept { ++calls; return WriteResult::Applied; }
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
} // namespace

int main()
{
    // Getter and setter share one C++ type; only the declared wire type differs.
    const Field f64Row{"f64", "", ScalarType::F64, &readFloat, &writeFloat};
    calls = 0;
    const auto r1 = f64Row.write(2.5);
    const auto read1 = f64Row.read<double>();
    std::printf("F64 row, float setter: write(2.5)=%s setter calls=%d, read<double>=%g, writable()=%d\n",
                name(r1), calls, read1.value_or(-1), f64Row.writable());

    const Field u32Row{"u32", "", ScalarType::U32, &readUl, &writeUl};
    calls = 0;
    const auto r2 = u32Row.write(5);
    std::printf("U32 row, unsigned long setter (sizeof=%u): write(5)=%s setter calls=%d\n",
                unsigned(sizeof(unsigned long)), name(r2), calls);

    const Field sizeRow{"size", "", ScalarType::U32, &readSize, &writeSize};
    calls = 0;
    const auto r3 = sizeRow.write(5);
    std::printf("U32 row, size_t setter (sizeof=%u): write(5)=%s setter calls=%d\n",
                unsigned(sizeof(std::size_t)), name(r3), calls);

    // The factory form of the same callbacks infers the matching type and works.
    const FieldTable inferred{field("inferred", "", &readUl, &writeUl)};
    calls = 0;
    const auto r4 = inferred[0].write(5);
    std::printf("factory row (declared %u): write(5)=%s setter calls=%d\n",
                unsigned(static_cast<ScalarType>(inferred[0].declaredType)), name(r4), calls);

    // Persistent accepted although nothing can ever be read or written.
    const Field nullPersistent{"null", "", ScalarType::Null, &readScalar, &writeScalar,
                               FieldFlag::Persistent};
    const Field unknownPersistent{"unknown", "", static_cast<ScalarType>(200), &readScalar,
                                  &writeScalar, FieldFlag::Persistent};
    calls = 0;
    std::printf("Persistent Null row constructed: persistent=%d read type=%u write=%s calls=%d\n",
                nullPersistent.persistent(), unsigned(nullPersistent.read().type()),
                name(nullPersistent.write(1.0f)), calls);
    std::printf("Persistent unknown-tag row constructed: persistent=%d read type=%u write=%s calls=%d\n",
                unknownPersistent.persistent(), unsigned(unknownPersistent.read().type()),
                name(unknownPersistent.write(1.0f)), calls);
    return 0;
}
