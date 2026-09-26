// Review probe (commands slice): does a CommandTable returned by value from a
// factory function keep descriptor metadata pointers inside the final object?
// README: "In C++17 construct CommandTable directly: returning a
// self-referential table from a factory is not a portable constant expression."
#include "Telemetry.h"
#include <cstdint>
#include <cstdio>

using namespace telemetry;

static int calls = 0;
CommandResult run(float) noexcept { ++calls; return CommandResult::Executed; }

constexpr auto makeTable() noexcept
{
    return CommandTable{command<&run>("x", arg<0>("v", "V", 1.0f, 0.0f, 10.0f))};
}

#ifdef PROBE_CONSTEXPR_FACTORY
// Namespace-scope constant built through the factory.
constexpr auto fromFactory = makeTable();
#endif

// A runtime (non-constant) local obtained from the same factory.
auto runtimeFactory() noexcept { return makeTable(); }

template <class Table>
bool inside(const Table& table, const void* p)
{
    const auto lo = reinterpret_cast<std::uintptr_t>(&table);
    const auto hi = lo + sizeof(Table);
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    return v >= lo && v < hi;
}

int main()
{
    int bad = 0;
#ifdef PROBE_CONSTEXPR_FACTORY
    std::printf("constexpr factory: table=%p metadata=%p inside=%d\n",
                static_cast<const void*>(&fromFactory), fromFactory[0].metadata,
                inside(fromFactory, fromFactory[0].metadata));
    bad += !inside(fromFactory, fromFactory[0].metadata);
    bad += fromFactory.call<0>(11.0f) != CommandResult::InvalidValue;
    bad += fromFactory.index().execute(0, nullptr, 0) != CommandResult::ArgumentCountMismatch;
    const Scalar eleven[] = {11.0f};
    bad += fromFactory.index().execute(0, eleven, 1) != CommandResult::InvalidValue;
#endif
    const auto local = runtimeFactory();
    std::printf("runtime factory: table=%p metadata=%p inside=%d\n",
                static_cast<const void*>(&local), local[0].metadata, inside(local, local[0].metadata));
    bad += !inside(local, local[0].metadata);
    const Scalar eleven2[] = {11.0f};
    bad += local.index().execute(0, eleven2, 1) != CommandResult::InvalidValue;
    std::printf("%s (calls=%d)\n", bad ? "FAIL" : "OK", calls);
    return bad;
}
