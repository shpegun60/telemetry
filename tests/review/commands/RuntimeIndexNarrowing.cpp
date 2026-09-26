// Review probe (commands slice): compile-time positions are checked "before
// narrowing to size_t, including 64-bit enums on ARM32" (README). The runtime
// entry points take std::size_t (CommandTable::call) and CommandId = uint32_t
// (CommandIndex/CommandCatalogIndex::execute/find), so a 64-bit transport value
// narrows silently under -Wall -Wextra and can select a different row.
#include "Telemetry.h"
#include <cstdint>
#include <cstdio>

using namespace telemetry;

namespace {
int hits[2] = {0, 0};
CommandResult first() noexcept { ++hits[0]; return CommandResult::Executed; }
CommandResult second() noexcept { ++hits[1]; return CommandResult::Executed; }
constexpr CommandTable rows{command<&first>("first"), command<&second>("second")};
constexpr CommandCatalogTable groups{group("g", rows)};
} // namespace

// Keep the value opaque to the optimizer.
extern "C" __attribute__((noinline)) std::uint64_t wireValue(std::uint64_t v) noexcept { return v; }

extern "C" CommandResult runtime_position(std::uint64_t wide) noexcept
{
    return rows.call(wide); // size_t: 32-bit on ARM32, so 2^32 + 1 selects row 1.
}

int main()
{
    const std::uint64_t id = wireValue((std::uint64_t{1} << 32) | 1u); // not a valid ID
    const auto erased = rows.index().execute(id, nullptr, 0);          // CommandId narrows to 1
    const auto grouped = groups.index().execute(id, nullptr, 0);        // packed (0,1)
    const auto native = runtime_position(id);                           // NotFound on 64-bit hosts
    std::printf("erased=%d grouped=%d native=%d hits=%d,%d\n", static_cast<int>(erased),
                static_cast<int>(grouped), static_cast<int>(native), hits[0], hits[1]);
    return 0;
}
