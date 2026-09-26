// Construction rejects inconsistent custom metadata before serving any bytes.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "TestSupport.hpp"
#include <resource/telemetry/TelemetryFiles.hpp>
#include <telemetry/Telemetry.h>

enum class ReviewEnum : std::uint16_t
{
    Value
};
unsigned emittedEnums = 1, emittedParameters = 1, getterCalls = 0;
bool enumVisitSucceeds = true, parameterVisitSucceeds = true;
bool ignoreEnumSink = false, ignoreParameterSink = false;
unsigned acceptedEnums = 0, refusedEnums = 0, enumTraversals = 0;
unsigned acceptedParameters = 0, refusedParameters = 0;
std::array<unsigned, 3> parameterOrder{0, 1, 2};
constexpr unsigned noInvalidParameter = std::numeric_limits<unsigned>::max();
unsigned invalidParameterAt = noInvalidParameter;

bool visitEnum(void* context, telemetry::EnumEntrySink sink) noexcept
{
    ++enumTraversals;
    for (unsigned i = 0; i < emittedEnums; ++i)
    {
        const bool accepted =
            sink(context, telemetry::Scalar::fromU16(static_cast<std::uint16_t>(i)), "Value");
        acceptedEnums += accepted;
        refusedEnums += !accepted;
        if (!accepted && !ignoreEnumSink)
        {
            return false;
        }
    }
    return enumVisitSucceeds;
}

bool enumAt(std::uint32_t, void*, telemetry::EnumEntrySink) noexcept
{
    return false;
}

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

telemetry::CommandResult reviewCommand() noexcept
{
    return telemetry::CommandResult::Executed;
}

bool visitParameters(const void*, void* context, telemetry::CommandParamSink sink) noexcept
{
    for (unsigned i = 0; i < emittedParameters; ++i)
    {
        const auto type = i == invalidParameterAt
                              ? telemetry::FieldType{static_cast<telemetry::ScalarType>(255)}
                              : telemetry::enumType<ReviewEnum>();
        const telemetry::CommandParam parameter{parameterOrder[i], "p", "", type};
        const bool accepted = sink(context, parameter);
        acceptedParameters += accepted;
        refusedParameters += !accepted;
        if (!accepted && !ignoreParameterSink)
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

void resetObservations()
{
    acceptedEnums = refusedEnums = enumTraversals = 0;
    acceptedParameters = refusedParameters = 0;
}

void resetMetadata()
{
    enumOps.count = emittedEnums = parameterOps.count = emittedParameters = 1;
    enumVisitSucceeds = parameterVisitSucceeds = true;
    ignoreEnumSink = ignoreParameterSink = false;
    parameterOrder = {0, 1, 2};
    invalidParameterAt = noInvalidParameter;
    resetObservations();
}

template <class File>
void expectValidity(const File& file, bool valid)
{
    std::array<std::byte, 1024> bytes;
    bytes.fill(std::byte{0xa5});
    const auto result = file.read(0, bytes);
    CHECK((file.size() != 0) == valid);
    if (valid)
    {
        CHECK(result.status == resource::Status::Ok && result.eof && result.written == file.size());
    }
    else
    {
        CHECK(result.status == resource::Status::InvalidData && result.written == 0 &&
              result.next == 0 && !result.eof);
        CHECK(std::all_of(bytes.begin(), bytes.end(),
                          [](auto b)
                          {
                              return b == std::byte{0xa5};
                          }));
    }
}

template <class File>
void expectRejected(const File& file)
{
    expectValidity(file, false);
    if constexpr (requires { file.fingerprint(); })
    {
        CHECK(file.fingerprint() == 0);
    }
    const resource::Cursor cursors[]{7, resource::Cursor{1} << 62, resource::Cursor{2} << 62,
                                     resource::Cursor{3} << 62,
                                     std::numeric_limits<resource::Cursor>::max()};
    std::array<std::byte, 64> bytes;
    bytes.fill(std::byte{0xa5});
    for (const auto cursor : cursors)
    {
        const auto result = file.read(cursor, bytes);
        CHECK(result.status == resource::Status::InvalidData && result.written == 0 &&
              result.next == cursor && !result.eof);
        CHECK(std::all_of(bytes.begin(), bytes.end(),
                          [](auto b)
                          {
                              return b == std::byte{0xa5};
                          }));
    }
}

void checkIgnoredSinks(const telemetry::CatalogIndex& index,
                       const telemetry::CommandCatalogIndex& commandIndex)
{
    struct ParameterCase
    {
        unsigned advertised, emitted;
        std::array<unsigned, 3> order;
        unsigned invalidAt, accepted, refused, enumVisits;
    };

    const ParameterCase cases[]{
        {1, 2, {0, 0, 2}, noInvalidParameter, 1, 1, 1}, // Duplicate after a counted entry.
        {0, 1, {7, 1, 2}, noInvalidParameter, 0, 1, 0}, // Refused entry with advertised zero.
        {2, 3, {0, 0, 1}, noInvalidParameter, 1, 2, 1}, // Valid-looking entry after duplicate.
        {2, 3, {7, 0, 1}, noInvalidParameter, 0, 3, 0}, // Valid-looking entries after bad index.
        {1, 3, {0, 1, 2}, noInvalidParameter, 1, 2, 1}, // Entries beyond the advertised count.
        {2, 3, {0, 0, 1}, 0, 0, 3, 0},                  // Rejected type, then valid descriptions.
    };
    for (const auto& c : cases)
    {
        resetMetadata();
        parameterOps.count = c.advertised;
        emittedParameters = c.emitted;
        parameterOrder = c.order;
        invalidParameterAt = c.invalidAt;
        ignoreParameterSink = true;
        const telemetry_resource::CommandsFile file{commandIndex};
        CHECK(acceptedParameters == c.accepted && refusedParameters == c.refused);
        CHECK(enumTraversals == c.enumVisits);
        expectRejected(file);
    }

    // A nested traversal failure must survive an outer traversal that reports
    // success and repeatedly invokes the parameter visitor after its refusal.
    {
        resetMetadata();
        parameterOps.count = 2;
        emittedParameters = 3;
        parameterOrder = {0, 0, 1};
        enumVisitSucceeds = false;
        ignoreParameterSink = true;
        const telemetry_resource::CommandsFile file{commandIndex};
        CHECK(acceptedParameters == 0 && refusedParameters == 3);
        CHECK(enumTraversals == 1 && acceptedEnums == 1 && refusedEnums == 0);
        expectRejected(file);
    }

    // Enum visitors also latch their first refused entry. Later calls cannot
    // resume counting, and an outer parameter visitor cannot hide the refusal.
    for (unsigned advertised : {0u, 1u})
    {
        resetMetadata();
        enumOps.count = advertised;
        emittedEnums = advertised + 2;
        ignoreEnumSink = true;
        const telemetry_resource::SchemaFile schema{index};
        CHECK(acceptedEnums == advertised && refusedEnums == 2 && enumTraversals == 1);
        expectRejected(schema);
        expectRejected(telemetry_resource::ValuesFile{schema});
        expectRejected(telemetry_resource::ValuesFile{index});

        resetObservations();
        parameterOps.count = emittedParameters = 2;
        ignoreParameterSink = true;
        const telemetry_resource::CommandsFile file{commandIndex};
        CHECK(acceptedParameters == 0 && refusedParameters == 2 && enumTraversals == 1 &&
              acceptedEnums == advertised && refusedEnums == 2);
        expectRejected(file);
    }

    // Ignoring return values is not itself invalid when every emitted record
    // is accepted and both advertised counts describe the complete traversal.
    {
        resetMetadata();
        enumOps.count = emittedEnums = parameterOps.count = emittedParameters = 2;
        ignoreEnumSink = ignoreParameterSink = true;
        const telemetry_resource::SchemaFile schema{index};
        CHECK(acceptedEnums == 2 && refusedEnums == 0 && enumTraversals == 1);
        expectValidity(schema, true);
        CHECK(schema.fingerprint() != 0);

        resetObservations();
        const telemetry_resource::CommandsFile file{commandIndex};
        CHECK(acceptedParameters == 2 && refusedParameters == 0 && enumTraversals == 2 &&
              acceptedEnums == 4 && refusedEnums == 0);
        expectValidity(file, true);
        CHECK(file.fingerprint() != 0);
    }
}

int main()
{
    using namespace telemetry;
    static constexpr FieldTable rows{field<&readEnum>("e", "")};
    const Catalog catalogs[]{Catalog{"g", rows.data(), rows.size()}};
    const CatalogIndex index{catalogs};
    const auto command =
        detail::CommandBinding<&reviewCommand, void, detail::NoCommandArgs>::make(true, true);
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
        const auto reserved =
            detail::CommandBinding<&reviewCommand, void, detail::NoCommandArgs>::make(false,
                                                                                      described);
        const CommandCatalog reservedCatalogs[]{CommandCatalog{"r", &reserved, 1}};
        const CommandCatalogIndex reservedIndex{reservedCatalogs};
        for (unsigned count : {0u, 1u})
        {
            parameterOps.count = emittedParameters = count;
            expectValidity(telemetry_resource::CommandsFile{reservedIndex}, !described);
        }
    }
    checkIgnoredSinks(index, commandIndex);
    CHECK(getterCalls == 0);
    std::printf("Resource metadata contracts: %u checks passed\n", checks);
}
