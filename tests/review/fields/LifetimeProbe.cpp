// Review probe (fields slice): does every temporary-owner form stay rejected?
// Compile with -DCASE=N -fsyntax-only. "compiles" means the library accepted it.
// Cases 1..10 use a temporary whose conversion operator returns a reference to
// its own subobject; the resulting binding would dangle at the end of the
// full-expression. Cases 11..17 are controls that the library should reject.
#include "Telemetry.h"
#include <utility>

using namespace telemetry;

struct Owner {
    int value = 9;
    int read() const noexcept { return value; }
    WriteResult write(int next) const noexcept { (void)next; return WriteResult::Applied; }
    Scalar scalarRead() const noexcept { return value; }
    WriteResult scalarWrite(const Scalar&) const noexcept { return WriteResult::Applied; }
};
[[maybe_unused]] WriteResult writeAdapter(const Owner&, const Scalar&) noexcept { return WriteResult::Applied; }
[[maybe_unused]] int adapter(const Owner& owner) noexcept { return owner.value; }

// A temporary that hands out an lvalue reference into itself.
struct Holder {
    Owner inner;
    operator const Owner&() const noexcept { return inner; }
};
struct MutableHolder {
    Owner inner;
    operator Owner&() noexcept { return inner; }
};
struct DerivedOwner : Owner {};
struct ValueHolder {
    operator Owner() const noexcept { return Owner{}; }
};

struct Read {
    const Owner* source;
    int operator()() const noexcept { return source->read(); }
};
struct Write {
    const Owner* source;
    WriteResult operator()(int v) const noexcept { return source->write(v); }
};
struct ScalarRead {
    const Owner* source;
    Scalar operator()() const noexcept { return source->scalarRead(); }
};
const Owner owner{};
struct ReadHolder {
    Read inner{&owner};
    operator const Read&() const noexcept { return inner; }
};
struct WriteHolder {
    Write inner{&owner};
    operator const Write&() const noexcept { return inner; }
};
struct ScalarReadHolder {
    ScalarRead inner{&owner};
    operator const ScalarRead&() const noexcept { return inner; }
};
struct DerivedRead : Read {};
[[maybe_unused]] Owner lvalueOwner;
[[maybe_unused]] const Read readClosure{&owner};

#if CASE == 1
auto probe = Getter::bind<&Owner::read, const Owner>(Holder{});
#elif CASE == 2
auto probe = Getter::bind<&Owner::read, Owner>(MutableHolder{});
#elif CASE == 3
auto probe = Setter::bind<&Owner::scalarWrite, const Owner>(Holder{});
#elif CASE == 4
auto probe = Getter::bindContext<&adapter, const Owner>(Holder{});
#elif CASE == 5
auto probe = field<&Owner::read, nullptr, const Owner>("x", "", Holder{});
#elif CASE == 6
auto probe = field<&Owner::read, &Owner::write, const Owner>("x", "", Holder{});
#elif CASE == 7
auto probe = field<&Owner::scalarRead, nullptr, const Owner>("x", "", ScalarType::S32, Holder{});
#elif CASE == 8
auto probe = field<const Read>("x", "", ReadHolder{});
#elif CASE == 9
auto probe = field<const Read, const Write>("x", "", ReadHolder{}, WriteHolder{});
#elif CASE == 10
auto probe = field<const ScalarRead>("x", "", ScalarType::S32, ScalarReadHolder{});
#elif CASE == 11
auto probe = Getter::bind<&Owner::read, const Owner>(DerivedOwner{});
#elif CASE == 12
auto probe = field<&Owner::read, nullptr, const Owner>("x", "", DerivedOwner{});
#elif CASE == 13
auto probe = field<const Read>("x", "", DerivedRead{{&owner}});
#elif CASE == 14
OwnerSlot<const Owner> slot;
void probe() { slot.bind(Holder{}); }
#elif CASE == 15
auto probe = Getter::bind<&Owner::read>(std::move(lvalueOwner));
#elif CASE == 16
auto probe = field("x", "", std::move(readClosure));
#elif CASE == 17
auto probe = Getter::bind<&Owner::read, const Owner>(ValueHolder{});
#elif CASE == 18
auto probe = Setter::bindContext<&writeAdapter, const Owner>(Holder{});
#else
#error "Select CASE 1..18"
#endif
int main() {}
