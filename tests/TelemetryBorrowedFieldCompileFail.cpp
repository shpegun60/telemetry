// Borrowed field lifetimes: temporary rejection must survive explicit const
// template arguments. Case zero also exercises the accepted stable forms.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <cstdio>

#ifndef TELEMETRY_BORROWED_FIELD_FAIL_CASE
#define TELEMETRY_BORROWED_FIELD_FAIL_CASE 0
#endif

using namespace telemetry;
namespace {
struct Owner {
    mutable int value = 9;
    int read() const noexcept { return value; }
    WriteResult write(int next) const noexcept { value = next; return WriteResult::Applied; }
    Scalar scalarRead() const noexcept { return value; }
    WriteResult scalarWrite(const Scalar& next) const noexcept
    { value = next.get<std::int32_t>(); return WriteResult::Applied; }
};
struct Read {
    const Owner* source;
    int operator()() const noexcept { return source->read(); }
};
struct Write {
    const Owner* source;
    WriteResult operator()(int value) const noexcept { return source->write(value); }
};
struct ScalarRead {
    const Owner* source;
    Scalar operator()() const noexcept { return source->scalarRead(); }
};
struct ScalarWrite {
    const Owner* source;
    WriteResult operator()(const Scalar& value) const noexcept { return source->scalarWrite(value); }
};
const Owner owner{};
const Read readClosure{&owner};
const Write writeClosure{&owner};
const ScalarRead scalarRead{&owner};
const ScalarWrite scalarWrite{&owner};
} // namespace

#if TELEMETRY_BORROWED_FIELD_FAIL_CASE == 1
auto invalid = field("x", "", Read{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 2
auto invalid = field<const Read>("x", "", Read{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 3
auto invalid = field<const Read&>("x", "", Read{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 4
auto invalid = field("x", "", Read{&owner}, writeClosure);
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 5
auto invalid = field("x", "", readClosure, Write{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 6
auto invalid = field("x", "", Read{&owner}, Write{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 7
auto invalid = field<const Read, const Write>("x", "", Read{&owner}, writeClosure);
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 8
auto invalid = field<const Read, const Write>("x", "", readClosure, Write{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 9
auto invalid = field<const Read, const Write>("x", "", Read{&owner}, Write{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 10
auto invalid = field<const Read&, const Write>("x", "", Read{&owner}, writeClosure);
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 11
auto invalid = field<const Read, const Write&>("x", "", readClosure, Write{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 12
auto invalid = field<const Read&, const Write&>("x", "", Read{&owner}, Write{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 13
auto invalid = field("x", "", ScalarType::S32, ScalarRead{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 14
auto invalid = field<const ScalarRead>("x", "", ScalarType::S32, ScalarRead{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 15
auto invalid = field<const ScalarRead&>("x", "", ScalarType::S32, ScalarRead{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 16
auto invalid = field("x", "", ScalarType::S32, ScalarRead{&owner}, scalarWrite);
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 17
auto invalid = field("x", "", ScalarType::S32, scalarRead, ScalarWrite{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 18
auto invalid = field("x", "", ScalarType::S32, ScalarRead{&owner}, ScalarWrite{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 19
auto invalid = field<const ScalarRead, const ScalarWrite>("x", "", ScalarType::S32, ScalarRead{&owner}, scalarWrite);
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 20
auto invalid = field<const ScalarRead, const ScalarWrite>("x", "", ScalarType::S32, scalarRead, ScalarWrite{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 21
auto invalid = field<const ScalarRead, const ScalarWrite>("x", "", ScalarType::S32, ScalarRead{&owner}, ScalarWrite{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 22
auto invalid = field<const ScalarRead&, const ScalarWrite>("x", "", ScalarType::S32, ScalarRead{&owner}, scalarWrite);
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 23
auto invalid = field<const ScalarRead, const ScalarWrite&>("x", "", ScalarType::S32, scalarRead, ScalarWrite{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 24
auto invalid = field<const ScalarRead&, const ScalarWrite&>("x", "", ScalarType::S32, ScalarRead{&owner}, ScalarWrite{&owner});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 25
auto invalid = field<&Owner::read>("x", "", Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 26
auto invalid = field<&Owner::read, nullptr, const Owner>("x", "", Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 27
auto invalid = field<&Owner::read, nullptr, const Owner&>("x", "", Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 28
auto invalid = field<&Owner::read, &Owner::write>("x", "", Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 29
auto invalid = field<&Owner::read, &Owner::write, const Owner>("x", "", Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 30
auto invalid = field<&Owner::read, &Owner::write, const Owner&>("x", "", Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 31
auto invalid = field<&Owner::scalarRead>("x", "", ScalarType::S32, Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 32
auto invalid = field<&Owner::scalarRead, nullptr, const Owner>("x", "", ScalarType::S32, Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 33
auto invalid = field<&Owner::scalarRead, nullptr, const Owner&>("x", "", ScalarType::S32, Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 34
auto invalid = field<&Owner::scalarRead, &Owner::scalarWrite>("x", "", ScalarType::S32, Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 35
auto invalid = field<&Owner::scalarRead, &Owner::scalarWrite, const Owner>("x", "", ScalarType::S32, Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE == 36
auto invalid = field<&Owner::scalarRead, &Owner::scalarWrite, const Owner&>("x", "", ScalarType::S32, Owner{});
#elif TELEMETRY_BORROWED_FIELD_FAIL_CASE != 0
#error Unknown borrowed field failure case
#else
constexpr FieldTable safe{
    field("deduced", "", readClosure, writeClosure),
    field<const Read, const Write>("explicit", "", readClosure, writeClosure),
    field("scalar", "", ScalarType::S32, scalarRead, scalarWrite),
    field<const ScalarRead, const ScalarWrite>("explicit scalar", "", ScalarType::S32, scalarRead, scalarWrite),
    field<&Owner::read, &Owner::write>("owner", "", owner),
    field<&Owner::read, &Owner::write, const Owner>("explicit owner", "", owner),
    field<&Owner::scalarRead, &Owner::scalarWrite>("scalar owner", "", ScalarType::S32, owner),
    field<&Owner::scalarRead, &Owner::scalarWrite, const Owner>("explicit scalar owner", "", ScalarType::S32, owner),
    field<const Read>("read only", "", readClosure),
    field<const ScalarRead>("scalar read only", "", ScalarType::S32, scalarRead),
    field("limits", "", readClosure, limits(9, 1, 100)),
    field("inline", "", []() noexcept { return owner.read(); },
        [](int value) noexcept { return owner.write(value); }),
    field("plus", "", +[]() noexcept { return owner.read(); },
        +[](int value) noexcept { return owner.write(value); }),
};
template <std::size_t... I>
bool checkNative(std::index_sequence<I...>)
{
    return ((safe.write<I>(42) == WriteResult::Applied && safe.read<I, int>() == 42) && ...);
}
int main()
{
    bool ok = checkNative(std::make_index_sequence<8>{});
    for (std::size_t i = 0; i < safe.size(); ++i) {
        const bool writable = i < 8 || i > 10;
        const auto written = safe[i].write(21);
        ok = ok && written == (writable ? WriteResult::Applied : WriteResult::ReadOnly)
            && safe[i].read<int>() == 21;
    }
    ok = ok && safe.write<11>(43) == WriteResult::Applied && safe.read<11>() == 43
        && safe.write<12>(44) == WriteResult::Applied && safe.read<12>() == 44;
    std::printf("%s borrowed field lvalue and inline callback checks passed\n", ok ? "All" : "Not all");
    return ok ? 0 : 1;
}
#endif
