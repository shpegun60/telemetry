// Borrowed bindings reject braced temporaries and conversion-created objects.
// Case zero verifies the corresponding stable object and function forms.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <cstdio>
#include <initializer_list>
#include <utility>

#ifndef TELEMETRY_BORROWED_BRACE_FAIL_CASE
#define TELEMETRY_BORROWED_BRACE_FAIL_CASE 0
#endif

using namespace telemetry;
namespace brace_probe {
struct Owner {
    mutable int value = 9;
    mutable int calls = 0;
    int read() const noexcept { return value; }
    WriteResult write(int next) const noexcept
    { value = next; return WriteResult::Applied; }
    Scalar scalarRead() const noexcept { return value; }
    WriteResult scalarWrite(const Scalar& next) const noexcept
    { return write(next.get<std::int32_t>()); }
    CommandResult run() const noexcept
    { ++calls; return CommandResult::Executed; }
    CommandResult runWithValue(int next) const noexcept
    { value = next; ++calls; return CommandResult::Executed; }
};
struct Prefix { int prefix = 5; };
struct Derived : Prefix, Owner {};
const Owner owner{};
struct Read {
    const Owner* source = &owner;
    int operator()() const noexcept { return source->read(); }
};
struct Write {
    const Owner* source = &owner;
    WriteResult operator()(int next) const noexcept { return source->write(next); }
};
struct ScalarRead {
    const Owner* source = &owner;
    Scalar operator()() const noexcept { return source->scalarRead(); }
};
struct ScalarWrite {
    const Owner* source = &owner;
    WriteResult operator()(const Scalar& next) const noexcept
    { return source->scalarWrite(next); }
};
struct FloatRead {
    float value = 1.f;
    float operator()() const noexcept { return value; }
};
struct Run {
    const Owner* source = &owner;
    CommandResult operator()() const noexcept { return source->run(); }
};
struct RunWithValue {
    const Owner* source = &owner;
    CommandResult operator()(int next) const noexcept { return source->runWithValue(next); }
};
const Read readClosure{};
const Write writeClosure{};
const ScalarRead scalarRead{};
const ScalarWrite scalarWrite{};
const Run runClosure{};

int adapter(const Owner& value) noexcept { return value.read(); }
WriteResult writeAdapter(const Owner& value, const Scalar& next) noexcept
{ return value.scalarWrite(next); }
int listAdapter(const std::initializer_list<int>& values) noexcept
{ return *values.begin(); }
using ThreeInts = int[3];
int arrayAdapter(const ThreeInts& values) noexcept { return values[0]; }
WriteResult arrayWriteAdapter(const ThreeInts& values, const Scalar& value) noexcept
{ return value.get<std::int32_t>() == values[0] ? WriteResult::Applied : WriteResult::InvalidValue; }
struct ArrayProxy {
    ThreeInts values{5, 6, 7};
    operator const ThreeInts&() const noexcept { return values; }
};

// User-provided constructors make these nonaggregates, so {proxy} performs
// the conversion to a temporary object instead of initializing a data member.
struct ConvertedOwner : Owner { ConvertedOwner() noexcept {} };
struct ConvertedRead : Read { ConvertedRead() noexcept {} };
struct ConvertedWrite : Write { ConvertedWrite() noexcept {} };
struct ConvertedScalarRead : ScalarRead { ConvertedScalarRead() noexcept {} };
struct ConvertedScalarWrite : ScalarWrite { ConvertedScalarWrite() noexcept {} };
struct ConvertedRun : Run { ConvertedRun() noexcept {} };
template <class T> struct Proxy {
    operator T() const noexcept { return {}; }
};
Proxy<ConvertedOwner> ownerProxy;
Proxy<ConvertedRead> readProxy;
Proxy<ConvertedWrite> writeProxy;
Proxy<ConvertedScalarRead> scalarReadProxy;
Proxy<ConvertedScalarWrite> scalarWriteProxy;
Proxy<ConvertedRun> runProxy;
const ConvertedRead convertedRead{};
const ConvertedWrite convertedWrite{};
const ConvertedScalarRead convertedScalarRead{};
const ConvertedScalarWrite convertedScalarWrite{};

struct Convertible {
    int value = 17;
    using Function = int (*)() noexcept;
    static int constant() noexcept { return 71; }
    operator Function() const noexcept { return &constant; }
    int operator()() const noexcept { return value; }
};
} // namespace brace_probe
using namespace brace_probe;

#if TELEMETRY_BORROWED_BRACE_FAIL_CASE == 1
auto invalid = field<&Owner::read, nullptr, const Owner>("x", "", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 2
auto invalid = field<&Owner::read, &Owner::write, const Owner>("x", "", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 3
auto invalid = field<&Owner::scalarRead, nullptr, const Owner>("x", "", ScalarType::S32, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 4
auto invalid = Getter::bind<&Owner::read, const Owner>({});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 5
auto invalid = Setter::bind<&Owner::scalarWrite, const Owner>({});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 6
auto invalid = Getter::bindContext<&adapter, const Owner>({});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 7
auto invalid = Setter::bindContext<&writeAdapter, const Owner>({});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 8
auto invalid = field<const Read>("x", "", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 9
auto invalid = field<const Read, const Write>("x", "", {}, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 10
auto invalid = field<const ScalarRead>("x", "", ScalarType::S32, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 11
auto invalid = field<const Read, const Write>("x", "", readClosure, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 12
void invalid() { DelegateRefSlot<float() noexcept> slot; slot.bind<const FloatRead>({}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 13
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<&Owner::read, const Owner>({}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 14
void invalid() { OwnerSlot<const Owner> slot; slot.bind({}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 15
auto invalid = command<&Owner::run, const Owner>("run", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 16
auto invalid = command<const Run>("run", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 17
auto invalid = field<&Owner::read, nullptr, const Owner>("x", "", {7});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 18
auto invalid = field<const Read, const Write>("x", "", {}, writeClosure);
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 19
auto invalid = field<const ScalarRead, const ScalarWrite>("x", "", ScalarType::S32, {}, scalarWrite);
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 20
auto invalid = field<const ScalarRead, const ScalarWrite>("x", "", ScalarType::S32, scalarRead, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 21
auto invalid = field<const ScalarRead, const ScalarWrite>("x", "", ScalarType::S32, {}, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 22
auto invalid = field<&Owner::read, nullptr, const Owner, decltype(limits(9, 0, 100))>("x", "", {}, limits(9, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 23
auto invalid = field<const Read>("x", "", {}, limits(9, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 24
auto invalid = field<const Read, const Write>("x", "", {}, writeClosure, limits(9, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 25
auto invalid = field<const Read, const Write>("x", "", readClosure, {}, limits(9, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 26
auto invalid = field<const Read, const Write>("x", "", {}, {}, limits(9, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 27
auto invalid = field<&Owner::scalarRead, &Owner::scalarWrite, const Owner>("x", "", ScalarType::S32, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 28
auto invalid = command<&Owner::runWithValue, const Owner>("run", {}, arg<0>("value", "", 0, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 29
auto invalid = command<const RunWithValue>("run", {}, arg<0>("value", "", 0, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 30
auto invalid = Getter::bind<&Owner::read, const ConvertedOwner>({Proxy<ConvertedOwner>{}});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 31
auto invalid = Getter::bind<&Owner::read, const ConvertedOwner>({ownerProxy});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 32
auto invalid = Setter::bind<&Owner::scalarWrite, const ConvertedOwner>({ownerProxy});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 33
auto invalid = Getter::bindContext<&adapter, const ConvertedOwner>({ownerProxy});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 34
auto invalid = Setter::bindContext<&writeAdapter, const ConvertedOwner>({Proxy<ConvertedOwner>{}});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 35
auto invalid = field<&Owner::read, nullptr, const ConvertedOwner, decltype(limits(9, 0, 100))>("x", "", {ownerProxy}, limits(9, 0, 100));
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 36
auto invalid = field<&Owner::scalarRead, nullptr, const ConvertedOwner>("x", "", ScalarType::S32, {Proxy<ConvertedOwner>{}});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 37
auto invalid = command<&Owner::run, const ConvertedOwner>("run", {ownerProxy});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 38
auto invalid = field<const ConvertedRead>("x", "", {Proxy<ConvertedRead>{}});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 39
auto invalid = field<const ConvertedRead, const ConvertedWrite>("x", "", {readProxy}, convertedWrite);
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 40
auto invalid = field<const ConvertedRead, const ConvertedWrite>("x", "", convertedRead, {writeProxy});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 41
auto invalid = field<const ConvertedScalarRead, const ConvertedScalarWrite>("x", "", ScalarType::S32, {scalarReadProxy}, {scalarWriteProxy});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 42
auto invalid = command<const ConvertedRun>("run", {runProxy});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 43
void invalid() { OwnerSlot<const Owner> slot; slot.bind(ownerProxy); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 44
void invalid() { OwnerSlot<const ConvertedOwner> slot; slot.bind({ownerProxy}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 45
void invalid() { OwnerSlot<const ConvertedOwner> slot; slot.bind({Proxy<ConvertedOwner>{}}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 46
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const Convertible>(Convertible{}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 47
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const Convertible>({}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 48
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const Convertible>({7}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 49
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<&Owner::read, const ConvertedOwner>({ownerProxy}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 50
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const ConvertedRead>({Proxy<ConvertedRead>{}}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 51
auto invalid = DelegateRefSlot<int() noexcept>{}.get();
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 52
void invalid() { DelegateRefSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 53
void invalid() { const DelegateRefSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 54
auto invalid = DelegateSlot<int() noexcept>{}.get();
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 55
void invalid() { DelegateSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 56
void invalid() { const DelegateSlot<int() noexcept> slot; auto target = std::move(slot).get(); (void)target; }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 57
auto invalid = Getter::bindContext<&listAdapter, const std::initializer_list<int>>({1, 2, 3});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 58
auto invalid = Getter::bind<&Owner::read, const Owner&>({});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 59
auto invalid = field<const Read&>("x", "", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 60
auto invalid = command<const Run&>("run", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 61
auto invalid = field<const Read&, const Write>("x", "", {}, writeClosure);
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 62
auto invalid = field<const ScalarRead, const ScalarWrite&>("x", "", ScalarType::S32, scalarRead, {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 63
auto invalid = command<&Owner::run, const Owner&>("run", {});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 64
void invalid() { DelegateRefSlot<int() noexcept> slot; slot.bind<const Read&>({}); }
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 65
auto invalid = field<const ConvertedRead, const ConvertedWrite>("x", "", {convertedRead}, writeProxy);
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 66
auto invalid = field<const ConvertedRead, const ConvertedWrite>("x", "", readProxy, {convertedWrite});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 67
auto invalid = field<const ConvertedScalarRead, const ConvertedScalarWrite>("x", "", ScalarType::S32, {convertedScalarRead}, scalarWriteProxy);
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 68
auto invalid = field<const ConvertedScalarRead, const ConvertedScalarWrite>("x", "", ScalarType::S32, scalarReadProxy, {convertedScalarWrite});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 69
auto invalid = Getter::bindContext<&arrayAdapter, const ThreeInts>({1, 2, 3});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 70
auto invalid = Setter::bindContext<&arrayWriteAdapter, const ThreeInts>({1, 2, 3});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 71
auto invalid = Getter::bindContext<&arrayAdapter, const ThreeInts>({ArrayProxy{}});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE == 72
auto invalid = Setter::bindContext<&arrayWriteAdapter, const ThreeInts>({ArrayProxy{}});
#elif TELEMETRY_BORROWED_BRACE_FAIL_CASE != 0
#error Unknown borrowed brace failure case
#else
int main()
{
    bool ok = true;
    unsigned checks = 0;
    const auto check = [&](bool result) noexcept { ++checks; ok = ok && result; };
    Owner live{13};
    const Owner fixed{21};
    Derived derived;
    derived.value = 29;
    const Derived fixedDerived{};
    ConvertedOwner constructed;
    constructed.value = 37;

    check(Getter::bind<&Owner::read>(live)().get<std::int32_t>() == 13);
    check(Getter::bind<&Owner::read, const Owner>({live})().get<std::int32_t>() == 13);
    check(Getter::bind<&Owner::read, const Owner>({fixed})().get<std::int32_t>() == 21);
    check(Getter::bind<&Owner::read, const Owner>({derived})().get<std::int32_t>() == 29);
    check(Getter::bind<&Owner::read, const Owner>({fixedDerived})().get<std::int32_t>() == 9);
    check(Getter::bind<&Owner::read, const ConvertedOwner>({constructed})().get<std::int32_t>() == 37);
    check(Getter::bindContext<&adapter, const Owner>({live})().get<std::int32_t>() == 13);
    check(Setter::bind<&Owner::scalarWrite, const Owner>({live})(Scalar::from(15)) == WriteResult::Applied);
    check(live.value == 15);
    check(Setter::bindContext<&writeAdapter, const Owner>({live})(Scalar::from(18)) == WriteResult::Applied);
    check(live.value == 18);

    OwnerSlot<const Owner> ownerSlot;
    ownerSlot.bind({live});
    check(ownerSlot.get() == &live);
    ownerSlot.bind({derived});
    check(ownerSlot.get() == static_cast<const Owner*>(&derived));
    ownerSlot.bind({fixedDerived});
    check(ownerSlot.get() == static_cast<const Owner*>(&fixedDerived));

    const FieldTable stable{
        field<&Owner::read, nullptr, const Owner>("member", "", {live}),
        field<&Owner::read, &Owner::write, const Owner>("member rw", "", {live}),
        field<&Owner::scalarRead, &Owner::scalarWrite, const Owner>("scalar member", "", ScalarType::S32, {live}),
        field<const Read>("closure", "", {readClosure}),
        field<const Read, const Write>("pair", "", {readClosure}, {writeClosure}),
        field<const Read, const Write>("first braced", "", {readClosure}, writeClosure),
        field<const Read, const Write>("second braced", "", readClosure, {writeClosure}),
        field<const ScalarRead>("scalar closure", "", ScalarType::S32, {scalarRead}),
        field<const ScalarRead, const ScalarWrite>("scalar pair", "", ScalarType::S32, {scalarRead}, {scalarWrite}),
        field<const Read>("metadata", "", {readClosure}, limits(9, 0, 100)),
        field<const Read, const Write>("pair metadata", "", {readClosure}, {writeClosure}, limits(9, 0, 100)),
        field<&Owner::read, nullptr, const Owner>("member metadata", "", {live}, limits(9, 0, 100)),
        field<&Owner::read, nullptr, const Owner, decltype(limits(9, 0, 100))>("explicit member metadata", "", {live}, limits(9, 0, 100)),
        field<const ScalarRead>("braced scalar type", "", {ScalarType::S32}, {scalarRead}),
        field<const ScalarRead>("braced field type", "", {FieldType{ScalarType::S32}}, {scalarRead}),
    };
    check(stable.read<0, int>() == 18);
    check(stable.write<1>(23) == WriteResult::Applied && live.value == 23);
    check(stable.write<2>(24) == WriteResult::Applied && stable.read<2, int>() == 24);
    check(stable.read<3, int>() == 9);
    check(stable.write<4>(31) == WriteResult::Applied && stable.read<4, int>() == 31);
    check(stable.write<5>(32) == WriteResult::Applied && stable.read<5, int>() == 32);
    check(stable.write<6>(33) == WriteResult::Applied && stable.read<6, int>() == 33);
    check(stable.read<7, int>() == 33);
    check(stable.write<8>(34) == WriteResult::Applied && stable.read<8, int>() == 34);
    check(stable.read<9, int>() == 34);
    check(stable.write<10>(35) == WriteResult::Applied && stable.read<10, int>() == 35);
    check(stable.read<11, int>() == 24);
    check(stable[4].read<int>() == 35);
    check(stable[8].write(36) == WriteResult::Applied && owner.value == 36);
    check(stable.read<12, int>() == 24);
    check(stable.read<13, int>() == 36);
    check(stable.read<14, int>() == 36);

    const RunWithValue runWithValue{&live};
    const CommandTable commands{
        command<&Owner::run, const Owner>("member", {live}),
        command<const Run>("closure", {runClosure}),
        command<&Owner::runWithValue, const Owner>("member metadata", {live}, arg<0>("value", "", 0, 0, 100)),
        command<const RunWithValue>("closure metadata", {runWithValue}, arg<0>("value", "", 0, 0, 100)),
    };
    check(commands.call<0>() == CommandResult::Executed && live.calls == 1);
    check(commands.call<1>() == CommandResult::Executed && owner.calls == 1);
    check(commands.call<2>(41) == CommandResult::Executed && live.value == 41);
    check(commands.call<3>(42) == CommandResult::Executed && live.value == 42);

    DelegateRefSlot<int() noexcept> borrowed;
    borrowed.bind<&Owner::read, const Owner>({live});
    check(borrowed.invoke() == 42);
    borrowed.bind<&Owner::read, const Owner>({derived});
    check(borrowed.invoke() == 29);
    borrowed.bind<const Read>({readClosure});
    check(borrowed.invoke() == 36);
    auto borrowedTarget = borrowed.get();
    check(static_cast<bool>(borrowedTarget) && borrowedTarget.invoke() == 36);
    borrowed.bind([]() noexcept { return 53; });
    check(borrowedTarget.invoke() == 53);
    borrowed.bind(Convertible{});
    check(borrowed.invoke() == Convertible::constant());
    const Convertible convertible{19};
    borrowed.bind<const Convertible>({convertible});
    check(borrowed.invoke() == 19);
    const auto& constBorrowed = borrowed;
    check(constBorrowed.get().invoke() == 19);

    DelegateSlot<int() noexcept> owned;
    owned.bind([value = 61]() noexcept { return value; });
    auto ownedTarget = owned.get();
    check(static_cast<bool>(ownedTarget) && ownedTarget.invoke() == 61);
    const auto& constOwned = owned;
    check(constOwned.get().invoke() == 61);
    check(!ContextFunctionSlot<int() noexcept>{}.get());
    check(FunctionSlot<int() noexcept>{}.get() == nullptr);

    const std::initializer_list<int> stableNumbers{2, 3};
    const auto fromList = Getter::bindContext<&listAdapter, const std::initializer_list<int>>({stableNumbers});
    check(fromList().get<std::int32_t>() == 2);
    ThreeInts numbers{7, 8, 9};
    const ThreeInts fixedNumbers{11, 12, 13};
    check(Getter::bindContext<&arrayAdapter, const ThreeInts>({numbers})().get<std::int32_t>() == 7);
    check(Getter::bindContext<&arrayAdapter, const ThreeInts>({fixedNumbers})().get<std::int32_t>() == 11);
    check(Setter::bindContext<&arrayWriteAdapter, const ThreeInts>({numbers})(Scalar::from(7)) == WriteResult::Applied);
    check(Setter::bindContext<&arrayWriteAdapter, const ThreeInts>({fixedNumbers})(Scalar::from(11)) == WriteResult::Applied);
    std::printf("%s %u borrowed brace and stable target checks passed\n", ok ? "All" : "Not all", checks);
    return ok ? 0 : 1;
}
#endif
