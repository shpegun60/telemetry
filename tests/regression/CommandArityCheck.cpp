// Regression promoted from tests/review/commands/ArityMatrix.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review probe (commands slice): ArgumentCountMismatch / null pointer / reserved /
// empty-slot ordering on every dispatch path, and "no owner side effect before
// all arguments pass" for multi-argument commands.
#include "Telemetry.h"
#include <cstdio>
#include <limits>

using namespace telemetry;

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* label)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}

enum class Mode : std::uint8_t { A, B, C };
struct Dev {
    int calls = 0;
    float v = -1;
    Mode m = Mode::A;
    std::int16_t s = 0;
    CommandResult zero() noexcept { ++calls; return CommandResult::Executed; }
    CommandResult three(float a, Mode b, std::int16_t c) noexcept
    { ++calls; v = a; m = b; s = c; return CommandResult::Executed; }
};
Dev dev;
OwnerSlot<Dev> slot;

constexpr CommandTable table{
    command<&Dev::zero>("zero", dev),
    command<&Dev::three>("three", dev, arg<2>("s", "", std::int16_t{0}, std::int16_t{-5}, std::int16_t{5})),
    reservedCommand(),
    command<&Dev::three>("slotThree", slot),
    command<&Dev::zero>("slotZero", slot),
};
constexpr CommandCatalogTable catalog{group("g", table)};
constexpr CommandIndex index = table.index();
constexpr CommandCatalogIndex grouped = catalog.index();
} // namespace

int main()
{
    const Scalar good3[] = {1.0f, std::uint8_t{1}, std::int16_t{2}};
    const Scalar lateBad3[] = {1.0f, std::uint8_t{1}, std::int16_t{6}}; // third fails limits
    const Scalar midBad3[] = {1.0f, std::uint8_t{9}, std::int16_t{2}};  // second fails enum bound
    const Scalar four[] = {1.0f, std::uint8_t{1}, std::int16_t{2}, std::int16_t{3}};

    // 1. Erased path, count around the arity, null pointer.
    expect(index.execute(1, good3, 3) == CommandResult::Executed && dev.calls == 1, "erased exact count");
    expect(index.execute(1, good3, 2) == CommandResult::ArgumentCountMismatch, "erased count-1");
    expect(index.execute(1, four, 4) == CommandResult::ArgumentCountMismatch, "erased count+1");
    expect(index.execute(1, nullptr, 3) == CommandResult::InvalidValue, "erased null, count==arity");
    expect(index.execute(1, nullptr, 0) == CommandResult::ArgumentCountMismatch, "erased null, count 0");
    expect(index.execute(0, nullptr, 0) == CommandResult::Executed && dev.calls == 2, "zero-arity null/0");
    expect(index.execute(0, good3, 3) == CommandResult::ArgumentCountMismatch, "zero-arity count 3");
    expect(index.execute(0, nullptr, 1) == CommandResult::ArgumentCountMismatch, "zero-arity null count 1");
    expect(index.execute(2, nullptr, 0) == CommandResult::Unavailable, "reserved erased 0");
    expect(index.execute(2, nullptr, 99) == CommandResult::Unavailable, "reserved erased 99, null");
    expect(index.execute(5, good3, 3) == CommandResult::NotFound, "erased past end");
    expect(index.execute(std::numeric_limits<CommandId>::max(), good3, 3) == CommandResult::NotFound, "erased max id");
    const int before = dev.calls;
    expect(index.execute(1, lateBad3, 3) == CommandResult::InvalidValue && dev.calls == before
           && dev.v == 1.0f && dev.s == 2, "erased: third argument fails, no owner side effect");
    expect(index.execute(1, midBad3, 3) == CommandResult::InvalidValue && dev.calls == before,
           "erased: second argument fails, no owner side effect");

    // 2. Grouped erased path and packed-ID bounds.
    expect(grouped.execute(makeId(0, 1), good3, 3) == CommandResult::Executed, "grouped exact");
    expect(grouped.execute(makeId(1, 0), good3, 3) == CommandResult::NotFound, "grouped bad group");
    expect(grouped.execute(makeId(0, 5), good3, 3) == CommandResult::NotFound, "grouped bad entry");
    expect(grouped.execute(makeId(0xffff, 0xffff), good3, 3) == CommandResult::NotFound, "grouped max id");
    expect(grouped.execute(makeId(0, 2), nullptr, 7) == CommandResult::Unavailable, "grouped reserved");

    // 3. Native runtime-position path.
    const int b2 = dev.calls;
    expect(table.call(std::size_t{1}, 1.0f, Mode::B) == CommandResult::ArgumentCountMismatch, "runtime count-1");
    expect(table.call(std::size_t{1}, 1.0f, Mode::B, 1, 2) == CommandResult::ArgumentCountMismatch, "runtime count+1");
    expect(table.call(std::size_t{0}, 1) == CommandResult::ArgumentCountMismatch, "runtime zero-arity with one");
    expect(table.call(std::size_t{2}) == CommandResult::Unavailable, "runtime reserved 0 args");
    expect(table.call(std::size_t{2}, 1, 2, 3, 4, 5) == CommandResult::Unavailable, "runtime reserved 5 args");
    expect(table.call(std::size_t{5}) == CommandResult::NotFound, "runtime past end");
    expect(table.call(static_cast<std::size_t>(-1), 1.0f, Mode::B, 1) == CommandResult::NotFound, "runtime SIZE_MAX");
    expect(table.call(std::size_t{1}, 2.0f, Mode::B, 6) == CommandResult::InvalidValue && dev.calls == b2,
           "runtime: third fails");
    expect(table.call(std::size_t{1}, 2.0, 7, 1) == CommandResult::InvalidValue && dev.calls == b2,
           "runtime: second fails (int -> Mode)");
    expect(table.call<1>(2.0, 1, 5.9) == CommandResult::Executed && dev.s == 5, "typed: fractional truncates into limit");
    expect(table.call<1>(2.0, 1, -5.9) == CommandResult::Executed && dev.s == -5, "typed: negative fraction truncates");
    expect(table.call<1>(2.0, 1, -6.0) == CommandResult::InvalidValue, "typed: -6 outside limit");
    expect(table.call<2>() == CommandResult::Unavailable && table.call<2>(1.0, 2) == CommandResult::Unavailable,
           "typed reserved accepts any arity");
    expect(catalog.call<makeId(0, 2)>(1, 2, 3) == CommandResult::Unavailable, "global typed reserved");

    // 4. Empty OwnerSlot: count checks first, then Unavailable before conversion.
    expect(index.execute(3, good3, 3) == CommandResult::Unavailable, "slot empty erased valid");
    expect(index.execute(3, lateBad3, 3) == CommandResult::Unavailable, "slot empty erased invalid -> Unavailable");
    expect(index.execute(3, good3, 2) == CommandResult::ArgumentCountMismatch, "slot empty erased bad count");
    expect(index.execute(3, nullptr, 3) == CommandResult::InvalidValue, "slot empty erased null pointer");
    expect(table.call<3>(1.0f, Mode::A, std::int16_t{1}) == CommandResult::Unavailable, "slot empty typed exact");
    expect(table.call<3>(1e300, 99, 1e9) == CommandResult::Unavailable, "slot empty typed invalid -> Unavailable");
    expect(table.call(std::size_t{3}, 1e300, 99, 1e9) == CommandResult::Unavailable, "slot empty runtime invalid");
    expect(table.call<4>() == CommandResult::Unavailable, "slot empty zero-arity");
    Dev other;
    slot.bind(other);
    expect(index.execute(3, midBad3, 3) == CommandResult::InvalidValue && other.calls == 0, "bound slot: invalid enum");
    expect(index.execute(3, good3, 3) == CommandResult::Executed && other.calls == 1, "bound slot: valid");
    expect(table.call<3>(2.0, 2, 3) == CommandResult::Executed && other.m == Mode::C && other.calls == 2,
           "bound slot typed");
    slot.reset();

    // 5. Scalar convenience (Command::call) with Scalar Null in a later position.
    expect(index.call(1, 1.0f, Mode::B, Scalar{}) == CommandResult::InvalidValue, "Null Scalar last arg");
    expect(index.call(1, Scalar::fromBool(true), Mode::B, std::int16_t{1}) == CommandResult::Executed
           && dev.v == 1.0f, "Bool Scalar to float param is accepted as 1.0");

    // 6. Name uniqueness helper over a table that has reserved rows.
    constexpr CommandTable twoReserved{
        command<&Dev::zero>("a", dev), reservedCommand(), reservedCommand()};
    std::printf("commandNamesUnique(table with two reserved rows) = %d\n",
                commandNamesUnique(twoReserved.data(), twoReserved.size()));

    std::printf("%d/%d arity-matrix checks passed\n", checks - failures, checks);
    return failures != 0;
}
