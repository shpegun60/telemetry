// Review probe (commands slice): README says "Extracting views from temporaries
// is rejected". The deleted const&& overloads stop `CommandTable{...}.index()`,
// but std::data() and any helper taking const T& still extract a borrowed view
// from a temporary without a diagnostic. Build with ASan (use-after-scope) to
// observe the read of the dead table.
#include "Telemetry.h"
#include <cstdio>
#include <iterator>

using namespace telemetry;

namespace {
int hits = 0;
CommandResult act(float) noexcept { ++hits; return CommandResult::Executed; }

template <class Table>
CommandIndex indexOf(const Table& table) noexcept { return table.index(); }

CommandIndex viaHelper() noexcept
{
    // Compiles: the temporary is an lvalue inside indexOf().
    return indexOf(CommandTable{command<&act>("act", arg<0>("v", "", 1.0f, 0.0f, 2.0f))});
}

CommandIndex viaStdData() noexcept
{
    // Compiles: std::data(const C&) calls data() on an lvalue.
    const Command* rows = std::data(CommandTable{command<&act>("act", arg<0>("v", "", 1.0f, 0.0f, 2.0f))});
    return CommandIndex{rows, 1};
}
} // namespace

int main(int argc, char**)
{
    const CommandIndex a = argc > 1 ? viaStdData() : viaHelper();
    const Scalar one[] = {1.0f};
    // The descriptor array and its metadata tuple belonged to a destroyed temporary.
    const auto result = a.execute(0, one, 1);
    std::printf("result=%d hits=%d\n", static_cast<int>(result), hits);
    return 0;
}
