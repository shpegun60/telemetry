// Native setters share checked Scalar conversion for every binding form.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace telemetry;
unsigned checks = 0, writes = 0, reads = 0;
float stored = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)

float readValue() noexcept { ++reads; return stored; }
WriteResult writeValue(float value) noexcept { ++writes; stored = value; return WriteResult::Applied; }
struct Owner {
    float read() const noexcept { return readValue(); }
    WriteResult write(float value) noexcept { return writeValue(value); }
} owner;
struct Reader { float operator()() const noexcept { return readValue(); } } reader;
struct Writer { WriteResult operator()(float value) const noexcept { return writeValue(value); } } writer;

enum Unfixed { Below = -1, Zero, Above };
Unfixed readEnum() noexcept { return Zero; }
WriteResult writeEnum(Unfixed) noexcept { ++writes; return WriteResult::Applied; }
}

int main()
{
    using namespace telemetry;
    FunctionSlot<float() noexcept> readSlot;
    FunctionSlot<WriteResult(float) noexcept> writeSlot;
    OwnerSlot<Owner> ownerSlot;
    ContextFunctionSlot<float() noexcept> contextReadSlot;
    ContextFunctionSlot<WriteResult(float) noexcept> contextWriteSlot;
    DelegateRefSlot<float() noexcept> borrowedReadSlot;
    DelegateRefSlot<WriteResult(float) noexcept> borrowedWriteSlot;
    DelegateSlot<float() noexcept> ownedReadSlot;
    DelegateSlot<WriteResult(float) noexcept> ownedWriteSlot;
    readSlot.bind(&readValue);
    writeSlot.bind(&writeValue);
    ownerSlot.bind(owner);
    contextReadSlot.bind([](void* context) noexcept {
        return static_cast<Owner*>(context)->read();
    }, &owner);
    contextWriteSlot.bind([](void* context, float value) noexcept {
        return static_cast<Owner*>(context)->write(value);
    }, &owner);
    borrowedReadSlot.bind(reader);
    borrowedWriteSlot.bind(writer);
    ownedReadSlot.bind(reader);
    ownedWriteSlot.bind(writer);
    const Field rows[]{
        field("parameters", "", &readValue, &writeValue).materialize(),
        field<&readValue, &writeValue>("templates", "").materialize(),
        field<&Owner::read, &Owner::write>("methods", "", owner).materialize(),
        field("callables", "", reader, writer).materialize(),
        field("functions", "", readSlot, writeSlot).materialize(),
        field<&Owner::read, &Owner::write>("owner slot", "", ownerSlot).materialize(),
        field("context functions", "", contextReadSlot, contextWriteSlot).materialize(),
        field("borrowed delegates", "", borrowedReadSlot, borrowedWriteSlot).materialize(),
        field("owned delegates", "", ownedReadSlot, ownedWriteSlot).materialize(),
    };

    for (const Field& source : rows) {
        const Field manual{"manual", "", ScalarType::F64, source.get, source.set};
        const auto before = writes;
        CHECK(manual.write(2.5) == WriteResult::Applied && stored == 2.5f && writes == before + 1);
        CHECK(manual.write(1.e300) == WriteResult::InvalidValue && writes == before + 1);
        CHECK(manual.write(std::numeric_limits<double>::infinity()) == WriteResult::InvalidValue);
        CHECK(manual.write(std::numeric_limits<double>::quiet_NaN()) == WriteResult::InvalidValue);
        CHECK(writes == before + 1);

        const Field integral{"truncate", "", ScalarType::U16, source.get, source.set};
        CHECK(integral.write(12.7) == WriteResult::Applied && stored == 12.f);
        CHECK(integral.write(-1) == WriteResult::InvalidValue && stored == 12.f);

        const Field bounded{"bounds", "", numericType<double>(0.0, 0.0, 0.1), source.get, source.set};
        CHECK(bounded.write(0.1) == WriteResult::Applied && stored == static_cast<float>(0.1));
        // Descriptor limits are in F64. Its accepted endpoint then rounds in
        // the deliberately narrower native callback, just like a C++ cast.
        CHECK(static_cast<double>(stored) > 0.1);
        const auto limitedWrites = writes;
        CHECK(bounded.write(0.10000001) == WriteResult::InvalidValue && writes == limitedWrites);

        // Raw set is a callback, not the Field policy entry point. It performs
        // numeric conversion but deliberately has no descriptor/limits access.
        CHECK(bounded.set(Scalar::fromF64(100.0)) == WriteResult::Applied && stored == 100.f);
        CHECK(bounded.set(Scalar::null()) == WriteResult::InvalidValue);
        CHECK(bounded.set(Scalar::fromF64(1.e300)) == WriteResult::InvalidValue);
        CHECK(reads == 0);
    }

    const auto unscoped = field<&readEnum, &writeEnum>("enum", "").materialize();
    const auto before = writes;
    CHECK(unscoped.set(Scalar::fromF64(1.5)) == WriteResult::Applied && writes == before + 1);
    CHECK(unscoped.set(Scalar::fromS64(2)) == WriteResult::InvalidValue && writes == before + 1);
    CHECK(unscoped.set(Scalar::fromS64(-2)) == WriteResult::InvalidValue && writes == before + 1);
    writeSlot.reset();
    ownerSlot.reset();
    contextWriteSlot.reset();
    borrowedWriteSlot.reset();
    ownedWriteSlot.reset();
    CHECK(rows[4].set(Scalar::fromF64(1.e300)) == WriteResult::Unavailable);
    CHECK(rows[5].set(Scalar::fromF64(1.e300)) == WriteResult::Unavailable);
    CHECK(rows[6].set(Scalar::fromF64(1.e300)) == WriteResult::Unavailable);
    CHECK(rows[7].set(Scalar::fromF64(1.e300)) == WriteResult::Unavailable);
    CHECK(rows[8].set(Scalar::fromF64(1.e300)) == WriteResult::Unavailable);
    CHECK(rows[6].set(Scalar::null()) == WriteResult::Unavailable);
    CHECK(rows[7].set(Scalar::null()) == WriteResult::Unavailable);
    CHECK(rows[8].set(Scalar::null()) == WriteResult::Unavailable);
    CHECK(writes == before + 1 && reads == 0);
    std::printf("Native setter conversion: %u checks passed\n", checks);
}
