// Review probe (catalog-json): do the indexed views model C++20 input ranges,
// as "input-iterator semantics" suggests? Compile with -std=c++20.
#include "Telemetry.h"
#include <iterator>
#include <ranges>
using namespace telemetry;
static_assert(std::input_iterator<FieldRange::iterator>);
static_assert(std::sentinel_for<FieldRange::iterator, FieldRange::iterator>);
static_assert(std::ranges::input_range<FieldRange>);
static_assert(std::ranges::input_range<FieldCatalogRange>);
static_assert(std::ranges::input_range<const FieldCatalogRange>);
static_assert(!std::forward_iterator<FieldRange::iterator>);
int main() { return 0; }
