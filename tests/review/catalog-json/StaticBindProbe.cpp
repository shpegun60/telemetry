// Review probe (catalog-json): CatalogIndex::bind<...>() edge cases.
// CASE 1: bind to a FieldCatalogTable object instead of a raw Catalog array.
// CASE 2: explicit pointer/count larger than the real Field array; a
//         compile-time read inside the claimed count but past the array.
// CASE 3: same catalog; runtime find() of that ID (documented caller contract).
// CASE 4: bind to a std::array<Catalog, N>.
#include "Telemetry.h"
#include <array>
#include <cstdio>

using namespace telemetry;
float a() noexcept { return 1.f; }
float b() noexcept { return 2.f; }
constexpr FieldTable local{field<&a>("a", ""), field<&b>("b", "")};
constexpr FieldCatalogTable table{group("g", local)};
constexpr Field rows[] = {{"x", "", ScalarType::F32, &a}};
constexpr Catalog oversized[] = {{"o", rows, 3}};

int main()
{
#if CASE == 1
    constexpr auto fixed = CatalogIndex::bind<table>();
    const auto value = fixed.read<makeId(0, 1)>();
    std::printf("bind<FieldCatalogTable>: read<0,1> = %g, size %zu\n",
                static_cast<double>(value.value_or(-1)), fixed.size());
#elif CASE == 2
    constexpr auto fixed = CatalogIndex::bind<oversized>();
    const auto value = fixed.read<makeId(0, 2)>();
    std::printf("%g\n", static_cast<double>(value.value_or(-1)));
#elif CASE == 3
    constexpr CatalogIndex index{oversized};
    volatile FieldId id = makeId(0, 2);
    const Field* found = index.find(id);
    std::printf("runtime find beyond real array returned %p (rows+2 = %p)\n",
                static_cast<const void*>(found), static_cast<const void*>(rows + 2));
#elif CASE == 4
    static constexpr std::array<Catalog, 1> arrayCatalogs{{Catalog{"s", rows}}};
    constexpr auto fixed = CatalogIndex::bind<arrayCatalogs>();
    std::printf("%zu\n", fixed.size());
#endif
    return 0;
}
