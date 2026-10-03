// Unrelated user indexOf/groupOf helpers remain callable with telemetry visible.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Build and run each CASE=1..4. Broad deleted overloads once collided through
// using-directives and argument-dependent lookup on telemetry table types.
//   1  user template helper indexOf(const T&) + using namespace (the CM-F1 repro shape)
//   2  the same helper in the user's namespace, no using-directive: ADL on CommandTable
//   3  user indexOf(std::string_view) + using namespace, called with a string literal
//   4  user groupOf(const Row&) non-template: exact match still wins (control, compiles)
#include <telemetry/Telemetry.hpp>
#include <string_view>
#include "SharedSupport.hpp"

telemetry::CommandResult act() noexcept { return telemetry::CommandResult::Executed; }
[[maybe_unused]] static constexpr telemetry::CommandTable table{telemetry::command<&act>("act")};

#if CASE == 1
using namespace telemetry;
template <class Table>
auto indexOf(const Table& t) noexcept { return t.size(); }
int main() { CHECK(indexOf(table) == 1); reportChecks(); return 0; }
#elif CASE == 2
namespace app {
template <class Table>
auto indexOf(const Table& t) noexcept { return t.size(); }
int run() { return indexOf(table) == 1 ? 0 : 1; }
}
int main() { CHECK(app::run() == 0); reportChecks(); return 0; }
#elif CASE == 3
using namespace telemetry;
std::size_t indexOf(std::string_view name) noexcept { return name.size(); }
int main() { CHECK(indexOf("voltage") == 7); reportChecks(); return 0; }
#elif CASE == 4
using namespace telemetry;
struct Row { int group; };
int groupOf(const Row& row) noexcept { return row.group; }
int main() { CHECK(groupOf(Row{1}) == 1 && table.size() == 1); reportChecks(); return 0; }
#else
int main() { CHECK(table.size() == 1); reportChecks(); return 0; }
#endif
