// Callback counters prove direct resume locality independently of wall time.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "TestSupport.hpp"
#include <telemetry/Telemetry.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include <resource/telemetry/detail/BlockStream.hpp>

enum class Earlier : std::uint16_t
{
    Value
};
enum class Selected : std::uint16_t
{
    Value
};
unsigned enumVisits[2]{}, typeVisits[2]{}, getterVisits[2]{};

template <unsigned Which>
struct Observed
{
    static bool at(std::uint32_t i, void* context, telemetry::EnumEntrySink sink) noexcept
    {
        if (i != 0 || sink == nullptr)
        {
            return false;
        }
        ++enumVisits[Which];
        const auto code = telemetry::Scalar::fromU16(0);
        return sink(context, code, "Value");
    }

    static bool all(void* context, telemetry::EnumEntrySink sink) noexcept
    {
        return at(0, context, sink);
    }

    inline static constexpr telemetry::EnumOps ops{1, &all, &at};
};

// Test-only specializations of the existing friend factory give each dictionary
// counters. Production dictionaries have no instrumentation or mutable state.
namespace telemetry
{
template <>
constexpr FieldType enumType<Earlier>() noexcept
{
    if (!std::is_constant_evaluated())
    {
        ++typeVisits[0];
    }
    return FieldType{ScalarType::U16, &Observed<0>::ops}.withLimits(0, 0, 0);
}

template <>
constexpr FieldType enumType<Selected>() noexcept
{
    if (!std::is_constant_evaluated())
    {
        ++typeVisits[1];
    }
    return FieldType{ScalarType::U16, &Observed<1>::ops}.withLimits(0, 0, 0);
}
} // namespace telemetry

Earlier earlier() noexcept
{
    ++getterVisits[0];
    return Earlier::Value;
}

Selected selected() noexcept
{
    ++getterVisits[1];
    return Selected::Value;
}

telemetry::CommandResult first(Earlier) noexcept
{
    return telemetry::CommandResult::Executed;
}

telemetry::CommandResult last(Selected) noexcept
{
    return telemetry::CommandResult::Executed;
}

int main()
{
    using namespace telemetry;
    using namespace telemetry_resource;
    using telemetry_resource::detail::BlockKind;
    constexpr FieldTable before{field<&earlier>("Before", "")};
    constexpr FieldTable after{field<&selected>("After", "")};
    constexpr CommandTable commandsBefore{command<&first>("Before")};
    constexpr CommandTable commandsAfter{command<&last>("After")};
    std::vector<Catalog> catalogs;
    std::vector<CommandCatalog> commandCatalogs;
    catalogs.reserve(65536);
    commandCatalogs.reserve(65536);
    catalogs.emplace_back("first", before.data(), before.size());
    commandCatalogs.emplace_back("first", commandsBefore.data(), commandsBefore.size());
    for (unsigned group = 1; group < 65535; ++group)
    {
        catalogs.emplace_back("empty", nullptr, 0);
        commandCatalogs.emplace_back("empty", nullptr, 0);
    }
    catalogs.emplace_back("last", after.data(), after.size());
    commandCatalogs.emplace_back("last", commandsAfter.data(), commandsAfter.size());
    SchemaFile schema{CatalogIndex{catalogs.data(), catalogs.size()}};
    CommandsFile commands{CommandCatalogIndex{commandCatalogs.data(), commandCatalogs.size()}};
    ValuesFile values{schema};
    CHECK(schema.size() != 0 && commands.size() != 0 && values.size() == 26);
    std::fill(std::begin(enumVisits), std::end(enumVisits), 0);
    std::fill(std::begin(typeVisits), std::end(typeVisits), 0);
    const auto cursor = telemetry_resource::detail::pack(BlockKind::Entry, makeId(65535, 0));
    std::array<std::byte, 512> bytes{};
    auto r = schema.read(cursor, bytes);
    CHECK(r.status == resource::Status::Ok && r.eof &&
          r.next == telemetry_resource::detail::endCursor);
    CHECK(enumVisits[0] == 0 && enumVisits[1] == 1 && getterVisits[0] == 0 && getterVisits[1] == 0);
    r = commands.read(cursor, bytes);
    CHECK(r.status == resource::Status::Ok && r.eof);
    CHECK(typeVisits[0] == 0 && typeVisits[1] == 1 && enumVisits[0] == 0 && enumVisits[1] == 2);
    r = values.read(cursor, {bytes.data(), 3});
    CHECK(r.status == resource::Status::Ok && r.eof && r.written == 3);
    CHECK(getterVisits[0] == 0 && getterVisits[1] == 1 && enumVisits[1] == 2);
    for (auto bad : {telemetry_resource::detail::pack(BlockKind::Catalog, 65536),
                     telemetry_resource::detail::pack(BlockKind::Prefix, 1),
                     telemetry_resource::detail::pack(BlockKind::End, 1),
                     telemetry_resource::detail::pack(BlockKind::End, 0, 1),
                     telemetry_resource::detail::pack(BlockKind::Entry, makeId(1, 0))})
    {
        CHECK(schema.read(bad, bytes).status == resource::Status::InvalidCursor);
        CHECK(commands.read(bad, bytes).status == resource::Status::InvalidCursor);
        CHECK(values.read(bad, bytes).status == resource::Status::InvalidCursor);
    }
    CHECK(enumVisits[0] == 0 && typeVisits[0] == 0 && getterVisits[0] == 0 && getterVisits[1] == 1);
    const auto end = telemetry_resource::detail::endCursor;
    for (const auto result : {schema.read(end, {}), commands.read(end, {}), values.read(end, {})})
    {
        CHECK(result.status == resource::Status::Ok && result.eof && result.written == 0 &&
              result.next == end);
    }
    std::printf("Direct cursor locality: %u checks passed\n", checks);
}
