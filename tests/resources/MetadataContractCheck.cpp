// Construction rejects inconsistent custom metadata before serving any bytes.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "TestSupport.hpp"
#include <resource/telemetry/TelemetryFiles.hpp>
#include <telemetry/Telemetry.h>

enum class ReviewEnum : std::uint16_t { Value };
unsigned emittedEnums = 1, emittedParameters = 1, getterCalls = 0;
bool enumVisitSucceeds = true, parameterVisitSucceeds = true;

bool visitEnum(void* context, telemetry::EnumEntrySink sink) noexcept
{
    for (unsigned i = 0; i < emittedEnums; ++i)
    {
        if (!sink(context, telemetry::Scalar::fromU16(static_cast<std::uint16_t>(i)), "Value"))
        {
            return false;
        }
    }
    return enumVisitSucceeds;
}

bool enumAt(std::uint32_t, void*, telemetry::EnumEntrySink) noexcept { return false; }
telemetry::EnumOps enumOps{1, &visitEnum, &enumAt};

namespace telemetry
{
template <>
constexpr FieldType enumType<ReviewEnum>() noexcept
{
    return FieldType{ScalarType::U16, &enumOps}.withLimits(0, 0, 0);
}
} // namespace telemetry

ReviewEnum readEnum() noexcept
{
    ++getterCalls;
    return ReviewEnum::Value;
}

telemetry::CommandResult reviewCommand() noexcept { return telemetry::CommandResult::Executed; }

bool visitParameters(const void*, void* context, telemetry::CommandParamSink sink) noexcept
{
    for (unsigned i = 0; i < emittedParameters; ++i)
    {
        const telemetry::CommandParam parameter{i, "p", "", telemetry::enumType<ReviewEnum>()};
        if (!sink(context, parameter))
        {
            return false;
        }
    }
    return parameterVisitSucceeds;
}

bool parameterAt(const void*, std::uint32_t, void*, telemetry::CommandParamSink) noexcept
{
    return false;
}
telemetry::CommandParamOps parameterOps{1, &visitParameters, &parameterAt};

namespace telemetry::detail
{
// Like the existing MetadataSizeCheck, deliberately bypass normal factories to
// test the adapter's boundary. Such custom ops must remain stable after build.
template <>
struct CommandBinding<&reviewCommand, void, NoCommandArgs>
{
    static Command make(bool invokable, bool described) noexcept
    {
        const auto invoke = +[](const void*, const void*, const Scalar*, std::size_t) noexcept
        {
            return CommandResult::Executed;
        };
        return Command{"c", nullptr, nullptr, invokable ? invoke : nullptr,
                       described ? &parameterOps : nullptr};
    }
};
} // namespace telemetry::detail

template <class File>
void expectValidity(const File& file, bool valid)
{
    std::array<std::byte, 1024> bytes;
    bytes.fill(std::byte{0xa5});
    const auto result = file.read(0, bytes);
    CHECK((file.size() != 0) == valid);
    if (valid)
    {
        CHECK(result.status == resource::Status::Ok && result.eof &&
              result.written == file.size());
    }
    else
    {
        CHECK(result.status == resource::Status::InvalidData && result.written == 0 &&
              result.next == 0 && !result.eof);
        CHECK(std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == std::byte{0xa5}; }));
    }
}

int main()
{
    using namespace telemetry;
    static constexpr FieldTable rows{field<&readEnum>("e", "")};
    const Catalog catalogs[]{Catalog{"g", rows.data(), rows.size()}};
    const CatalogIndex index{catalogs};
    const auto command = detail::CommandBinding<&reviewCommand, void, detail::NoCommandArgs>::make(true, true);
    const CommandCatalog commands[]{CommandCatalog{"g", &command, 1}};
    const CommandCatalogIndex commandIndex{commands};

    // Too few and too many enum entries, in both field and parameter metadata.
    for (unsigned advertised : {0u, 1u, 2u})
    {
        enumOps.count = advertised;
        for (unsigned emitted : {0u, 1u, 2u})
        {
            emittedEnums = emitted;
            const telemetry_resource::SchemaFile schema{index};
            expectValidity(schema, advertised == emitted);
            const telemetry_resource::ValuesFile values{schema};
            if (advertised != emitted)
            {
                expectValidity(values, false);
            }
            expectValidity(telemetry_resource::CommandsFile{commandIndex}, advertised == emitted);
        }
    }
    enumOps.count = emittedEnums = 1;
    for (unsigned advertised : {0u, 1u, 2u})
    {
        parameterOps.count = advertised;
        for (unsigned emitted : {0u, 1u, 2u})
        {
            emittedParameters = emitted;
            expectValidity(telemetry_resource::CommandsFile{commandIndex}, advertised == emitted);
        }
    }
    parameterOps.count = emittedParameters = 1;
    enumVisitSucceeds = false;
    expectValidity(telemetry_resource::SchemaFile{index}, false);
    expectValidity(telemetry_resource::CommandsFile{commandIndex}, false);
    enumVisitSucceeds = true;
    parameterVisitSucceeds = false;
    expectValidity(telemetry_resource::CommandsFile{commandIndex}, false);
    parameterVisitSucceeds = true;

    for (bool described : {false, true})
    {
        const auto reserved = detail::CommandBinding<&reviewCommand, void, detail::NoCommandArgs>::make(false, described);
        const CommandCatalog reservedCatalogs[]{CommandCatalog{"r", &reserved, 1}};
        const CommandCatalogIndex reservedIndex{reservedCatalogs};
        for (unsigned count : {0u, 1u})
        {
            parameterOps.count = emittedParameters = count;
            expectValidity(telemetry_resource::CommandsFile{reservedIndex}, !described);
        }
    }
    CHECK(getterCalls == 0);
    std::printf("Resource metadata contracts: %u checks passed\n", checks);
}
