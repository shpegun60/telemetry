// Review probe (commands slice): ownership of the self-referential CommandTable.
// -DCASE=N; cases 1-4 must be rejected, 5-7 must compile (and run correctly).
#include "Telemetry.h"
#include <deque>
#include <optional>
#include <vector>
#include <cstdio>

using namespace telemetry;
CommandResult f(float) noexcept { return CommandResult::Executed; }
using Table = decltype(CommandTable{command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f))});
constexpr CommandTable global{command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f))};

bool inside(const Table& t) { const auto* p = static_cast<const char*>(t[0].metadata);
    return p >= reinterpret_cast<const char*>(&t) && p < reinterpret_cast<const char*>(&t + 1); }

int main()
{
#if CASE == 1   // structured binding by reference
    auto& [a, b] = global; (void) a; (void) b;
#elif CASE == 2 // std::vector needs MoveInsertable
    std::vector<Table> v; v.emplace_back(command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f)));
#elif CASE == 3 // local constexpr table whose descriptors point into automatic storage
    constexpr CommandTable local{command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f))};
    (void) local;
#elif CASE == 4 // swap needs move
    Table x{command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f))}, y{command<&f>("g", arg<0>("v", "", 1.0f, 0.0f, 2.0f))};
    std::swap(x, y);
#elif CASE == 5 // static constexpr local is fine
    static constexpr CommandTable local{command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f))};
    std::printf("static local inside=%d\n", inside(local));
#elif CASE == 6 // deque/optional construct in place; descriptors stay inside their element
    std::deque<Table> d;
    for (int i = 0; i < 100; ++i) d.emplace_back(command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f)));
    std::optional<Table> o; o.emplace(command<&f>("f", arg<0>("v", "", 1.0f, 0.0f, 2.0f)));
    bool all = inside(*o);
    for (const auto& t : d) all = all && inside(t);
    const Scalar three[] = {3.0f};
    std::printf("deque/optional inside=%d execute=%d\n", all, static_cast<int>(d[0].index().execute(0, three, 1)));
#elif CASE == 7 // local constexpr without metadata or runtime owner compiles (no self-pointer)
    constexpr CommandTable local{command<&f>("f")};
    std::printf("metadata-less local constexpr: metadata=%p\n", local[0].metadata);
#endif
    return 0;
}
