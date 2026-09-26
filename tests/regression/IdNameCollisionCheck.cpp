// Unrelated user indexOf/groupOf helpers remain callable with telemetry visible.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Build and run each CASE=1..4. Broad deleted overloads once collided through
// using-directives and argument-dependent lookup on telemetry table types.
//   1  user template helper indexOf(const T&) + using namespace (the CM-F1 repro shape)
//   2  the same helper in the user's namespace, no using-directive: ADL on CommandTable
//   3  user indexOf(std::string_view) + using namespace, called with a string literal
//   4  user groupOf(const Row&) non-template: exact match still wins (control, compiles)
#include "Telemetry.h"
#include <string_view>

telemetry::CommandResult act() noexcept { return telemetry::CommandResult::Executed; }
[[maybe_unused]] static constexpr telemetry::CommandTable table{telemetry::command<&act>("act")};

#if CASE == 1
using namespace telemetry;
template <class Table>
CommandIndex indexOf(const Table& t) noexcept { return t.index(); }
int main() { return indexOf(table).size() == 1 ? 0 : 1; }
#elif CASE == 2
namespace app {
template <class Table>
telemetry::CommandIndex indexOf(const Table& t) noexcept { return t.index(); }
int run() { return indexOf(table).size() == 1 ? 0 : 1; }
}
int main() { return app::run(); }
#elif CASE == 3
using namespace telemetry;
std::size_t indexOf(std::string_view name) noexcept { return name.size(); }
int main() { return indexOf("voltage") == 7 ? 0 : 1; }
#elif CASE == 4
using namespace telemetry;
struct Row { int group; };
int groupOf(const Row& row) noexcept { return row.group; }
int main() { return groupOf(Row{1}) == 1 && table.size() == 1 ? 0 : 1; }
#else
int main() { return table.size() == 1 ? 0 : 1; }
#endif
