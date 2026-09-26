// Factory definitions reject missing mandatory labels before publication.
// Low-level descriptors remain inspectable by fallible schema serializers.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
float source() noexcept { return 1.f; }
CommandResult action() noexcept { return CommandResult::Executed; }
constexpr FieldTable rows{field<&source>("", "")};
constexpr CommandTable commands{command<&action>("")};
#if CASE == 1
constexpr auto bad = field<&source>(nullptr, "");
#elif CASE == 2
constexpr auto bad = field<&source>("x", nullptr);
#elif CASE == 3
constexpr auto bad = field<&source>(0, "");
#elif CASE == 4
constexpr auto bad = field(nullptr, "", &source);
#elif CASE == 5
constexpr auto bad = field("x", nullptr, &source);
#elif CASE == 6
constexpr auto bad = field(Field{nullptr, "", ScalarType::F32, &source});
#elif CASE == 7
constexpr auto bad = group(nullptr, rows);
#elif CASE == 8
constexpr auto bad = group(0, commands);
#elif CASE == 0
constexpr FieldCatalogTable valid{group("", rows)};
constexpr CommandCatalogTable validCommands{group("", commands)};
static_assert(valid.size() == 1 && validCommands.size() == 1);
#else
#error Select CASE 0..8
#endif
int main() {}
