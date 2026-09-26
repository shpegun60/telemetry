// Review probe (catalog-json): makeId() narrows its arguments to 16 bits
// before any bounds check. A wider constant or runtime position silently
// aliases another entry instead of being rejected.
#include "Telemetry.h"
#include <cstdio>

using namespace telemetry;
float a() noexcept { return 1.f; }
float b() noexcept { return 2.f; }
constexpr FieldTable local{field<&a>("a", ""), field<&b>("b", "")};
constexpr FieldCatalogTable table{group("g", local)};

int main(int argc, char**)
{
#if CASE == 1
    // Constant out-of-range entry: 65537 -> 1. Does the compile-time route reject it?
    auto value = table.read<makeId(0, 65537)>();
    std::printf("read<makeId(0,65537)> = %g\n", static_cast<double>(value.value_or(-1)));
#elif CASE == 2
    // Runtime out-of-range entry: 65536 + argc (argc == 1) -> position 1.
    const std::size_t wide = 65536u + static_cast<std::size_t>(argc);
    const Field* found = table.find(makeId(0, static_cast<EntryOffset>(wide)));
    const Field* implicitNarrow = table.find(makeId(0, wide));
    std::printf("find(makeId(0,%zu)) -> %s / %s\n", wide, found ? found->name : "null",
                implicitNarrow ? implicitNarrow->name : "null");
#endif
    return 0;
}
