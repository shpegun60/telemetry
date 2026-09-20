// Flags do not affect native access; visitors/ranges instantiate only on demand.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;
struct PolicyProbe {
    std::uint16_t read() const noexcept;
    WriteResult write(std::uint16_t) noexcept;
};
extern PolicyProbe policyProbe;
constexpr auto definition = field<&PolicyProbe::read, &PolicyProbe::write>("value", "", policyProbe);
constexpr FieldTable plainPolicyProbe{definition};
extern constexpr FieldTable traversalFlagFields{definition.withFlags(FieldFlag::Persistent)};
constexpr FieldCatalogTable traversalCatalogs{group("group", traversalFlagFields)};
static_assert(sizeof(Field) == 96 && sizeof(Command) == 20 && Field::abiFlagsOffset() == 20);
static_assert(offsetof(Field, get) == 0 && offsetof(Field, readType) == 8
              && offsetof(Field, set) == 32 && offsetof(Field, declaredType) == 40);

extern "C" {
std::uint16_t flags_manual_read() noexcept { return plainPolicyProbe.read<0>().value_or(0); }
std::uint16_t flags_local_read() noexcept { return traversalFlagFields.read<0>().value_or(0); }
std::uint16_t flags_global_read() noexcept { return traversalCatalogs.read<0>().value_or(0); }
WriteResult flags_manual_write(std::uint16_t v) noexcept { return plainPolicyProbe.write<0>(v); }
WriteResult flags_local_write(std::uint16_t v) noexcept { return traversalFlagFields.write<0>(v); }
WriteResult flags_global_write(std::uint16_t v) noexcept { return traversalCatalogs.write<0>(v); }
std::uint32_t traversal_ids(const CatalogIndex& index) noexcept
{
    std::uint32_t result = 0;
    for (auto catalog : index.catalogs())
        for (auto entry : catalog.fields()) result += entry.id();
    return result;
}
std::uint32_t traversal_scalar_width(const Scalar& value)
{
    return value.visit([](const auto& number) -> std::uint32_t {
        if constexpr (std::is_same_v<std::decay_t<decltype(number)>, std::monostate>) return 0;
        else return sizeof(number);
    });
}
std::size_t traversal_parameters(const Command& command) noexcept
{
    std::size_t count = 0;
    const bool complete = command.forEachParameter([&](const CommandParam&) noexcept { ++count; return true; });
    return complete ? count : SIZE_MAX;
}
}
