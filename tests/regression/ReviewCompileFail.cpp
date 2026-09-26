// Invalid definitions must fail for their actual contract, not a secondary error.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include <functional>
#include <memory>
using namespace telemetry;
float reviewRead() noexcept { return 1.f; }
WriteResult reviewWrite(const Scalar&) noexcept { return WriteResult::Applied; }
CommandResult run() noexcept { return CommandResult::Executed; }
struct Owner {
    float value = 2.f;
    float read() const noexcept { return value; }
    CommandResult run() const noexcept { return CommandResult::Executed; }
} owner;
struct Holder {
    Owner value;
    operator const Owner&() const noexcept { return value; }
};
struct ValueHolder { operator Owner() const noexcept { return Owner{}; } } proxy;
struct Read { float operator()() const noexcept { return 1.f; } } reader;
struct ReadHolder { Read value; operator const Read&() const noexcept { return value; } } readProxy;
Owner* pointer = &owner;
std::unique_ptr<Owner> smart;
std::reference_wrapper<Owner> wrapped{owner};

#if CASE == 1
auto bad = command<&Owner::run>("run", pointer);
#elif CASE == 2
auto bad = command<&Owner::run>("run", smart);
#elif CASE == 3
auto bad = command<&Owner::run>("run", wrapped);
#elif CASE == 4
auto bad = command<&Owner::run, const Owner>("run", Holder{});
#elif CASE == 5
auto bad = Getter::bind<&Owner::read, const Owner>(proxy);
#elif CASE == 6
auto bad = field<&Owner::read, nullptr, const Owner>("v", "", proxy);
#elif CASE == 7
auto bad = command<&Owner::run, const Owner>("run", proxy);
#elif CASE == 8
using Definition = decltype(field<&reviewRead>("x", ""));
Definition bad{Field{"x", "", ScalarType::F32, &reviewRead}};
#elif CASE == 9
using Definition = decltype(field<&Owner::read>("x", "", owner));
Definition bad{Field{"x", "", ScalarType::F32, &reviewRead}};
#elif CASE == 10
constexpr Field bad{"x", "", ScalarType::Null, &reviewRead, &reviewWrite, FieldFlag::Persistent};
#elif CASE == 11
constexpr Field bad{"x", "", static_cast<ScalarType>(255), &reviewRead, &reviewWrite, FieldFlag::Persistent};
#elif CASE == 12
constexpr auto bad = makeId(0, 65536);
#elif CASE == 13
constexpr auto bad = makeId(UINT64_MAX, 0);
#elif CASE == 14
constexpr auto bad = makeId(-1, 0);
#elif CASE == 15
auto bad = makeId(0.0, 1);
#elif CASE == 16
auto bad = command<&run>(0);
#elif CASE == 17
auto bad = command<&run>(nullptr);
#elif CASE == 18
constexpr const char* name = nullptr;
constexpr auto bad = command<&run>(name);
#elif CASE == 19
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind([](auto value) noexcept { (void)value; }); }
#elif CASE == 20
void bad() { DelegateRefSlot<void(int&) noexcept> slot; auto f = [](auto value) noexcept { (void)value; }; slot.bind(f); }
#elif CASE == 21
void bad() { DelegateRefSlot<float() noexcept> slot; slot.bind<&Owner::read, const Owner>(Holder{}); }
#elif CASE == 22
void bad() { DelegateRefSlot<float() noexcept> slot; slot.bind<const Read>(ReadHolder{}); }
#elif CASE == 23
void bad() { DelegateRefSlot<float() noexcept> slot; slot.bind<&Owner::read>(wrapped); }
#elif CASE == 24
void bad() { DelegateRefSlot<float() noexcept> slot; slot.bind<&Owner::read, const Owner>(proxy); }
#elif CASE == 25
struct Mixed {
    template<class T> void operator()(T&) { }
    void operator()(int) noexcept {}
};
void bad() { DelegateSlot<void(int&) noexcept> slot; slot.bind(Mixed{}); }
#elif CASE == 0
auto good = field<&Owner::read>("v", "", owner);
auto commandGood = command<&Owner::run>("run", *pointer);
#else
#error Select CASE 0..25
#endif
int main() {}
