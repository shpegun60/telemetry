// The owning and borrowed slot contracts must fail before retaining bad targets.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
struct Owner {
    float read() noexcept { return 1.f; }
    float throwing() { return 1.f; }
};
struct Callable { float operator()() noexcept { return 1.f; } } callable;
using Ref = DelegateRefSlot<float() noexcept>;
using Own = DelegateSlot<float() noexcept, 32>;
using Context = ContextFunctionSlot<float() noexcept>;
Ref ref;
Own own;
Context context;
#if TELEMETRY_LATE_BOUND_FAIL_CASE == 1
ContextFunctionSlot<float()> bad;
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 2
DelegateRefSlot<float()> bad;
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 3
DelegateSlot<float(), 32> bad;
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 4
void bad() { ref.bind(Callable{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 5
void bad() { ref.bind<const Callable>(Callable{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 6
void bad() { ref.bind<&Owner::read>(Owner{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 7
void bad() { ref.bind<&Owner::read, const Owner>(Owner{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 8
void bad() { Owner owner; ref.bind<&Owner::throwing>(owner); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 9
void bad() { auto f = []() { return 1.f; }; ref.bind(f); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 10
void bad() { own.bind([]() { return 1.f; }); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 11
void bad() { context.bind([](void*) { return 1.f; }, nullptr); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 12
struct Big { char bytes[64]{}; float operator()() noexcept { return float(bytes[0]); } };
void bad() { own.bind(Big{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 13
struct alignas(64) Wide { float operator()() noexcept { return 1.f; } };
void bad() { own.bind(Wide{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 14
struct Copy { Copy() = default; Copy(const Copy&) noexcept(false) {} Copy(Copy&&) noexcept = default; float operator()() noexcept { return 1.f; } };
void bad() { Copy f; own.bind(f); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 15
struct Move { Move() = default; Move(Move&&) noexcept(false) {} float operator()() noexcept { return 1.f; } };
void bad() { own.bind(Move{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 16
struct Destruct { ~Destruct() noexcept(false) {} float operator()() noexcept { return 1.f; } };
void bad() { own.bind(Destruct{}); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 17
auto bad = field("", "", Context{});
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 18
auto bad = field("", "", Ref{});
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 19
auto bad = field("", "", Own{});
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 20
auto bad = command("", DelegateSlot<CommandResult() noexcept, 32>{});
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 21
auto bad = field<const Own>("", "", Own{});
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 22
DelegateSlot<WriteResult(std::uint16_t) noexcept, 32> mismatch;
auto bad = field("", "", ref, mismatch);
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 23
auto bad = context;
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 24
auto bad = std::move(ref);
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 25
auto bad = own;
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 26
void bad() { Own next; next = std::move(own); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 27
void bad() { DelegateSlot<const int&() noexcept> target; target.bind([]() noexcept { return 1; }); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 28
void bad() { static int value; DelegateSlot<const double&() noexcept> target; target.bind([]() noexcept -> int& { return value; }); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 29
void bad() { Owner* owner = nullptr; ref.bind<&Owner::read>(owner); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 30
void bad() { context.bind([](void*, int) noexcept { return 1.f; }, nullptr); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 31
void bad() { const Owner owner; context.bind([](void*) noexcept { return 1.f; }, &owner); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 32
DelegateSlot<float() noexcept, 8> bad;
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 33
auto bad = std::move(context);
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 34
void bad() { Ref next; next = ref; }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 35
void bad() { DelegateRefSlot<int() noexcept> target; auto f = []() noexcept { return 1.e30; }; target.bind(f); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 36
void bad() { DelegateSlot<int() noexcept> target; target.bind([]() noexcept { return 1.e30; }); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 37
void bad() { DelegateRefSlot<int(double) noexcept> target; auto f = [](int x) noexcept { return x; }; target.bind(f); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 38
void bad() { DelegateSlot<int(double) noexcept> target; target.bind([](int x) noexcept { return x; }); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 39
struct Narrow { int call(int x) noexcept { return x; } } narrow;
void bad() { DelegateRefSlot<int(double) noexcept> target; target.bind<&Narrow::call>(narrow); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 40
int narrow(int x) noexcept { return x; }
void bad() { DelegateRefSlot<int(double) noexcept> target; target.bind<&narrow>(); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 41
struct PreferNarrow {
    template <class T> int operator()(T) noexcept { return 1; }
    double operator()(int) noexcept { return 1.e30; }
} preferNarrow;
void bad() { DelegateRefSlot<int(int) noexcept> target; target.bind(preferNarrow); }
#elif TELEMETRY_LATE_BOUND_FAIL_CASE == 42
struct PreferNarrow {
    template <class T> int operator()(T) noexcept { return 1; }
    double operator()(int) noexcept { return 1.e30; }
};
void bad() { DelegateSlot<int(int) noexcept> target; target.bind(PreferNarrow{}); }
#else
#error Select TELEMETRY_LATE_BOUND_FAIL_CASE from 1 through 42
#endif
int main() {}
