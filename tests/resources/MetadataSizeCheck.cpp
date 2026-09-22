// Arithmetic record lengths are checked against independently executed encoders.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "TestSupport.hpp"
#include <telemetry/Telemetry.h>
#include <resource/telemetry/detail/Metadata.hpp>
#include <resource/telemetry/TelemetryFiles.hpp>

telemetry::CommandResult invalidParameterTarget() noexcept
{
    return telemetry::CommandResult::Failed;
}

namespace telemetry::detail
{
// Test-only friend specialization supplies an invalid low-level descriptor.
// No new public metadata mutation or construction hook is needed for this test.
template <>
struct CommandBinding<&invalidParameterTarget, void, NoCommandArgs>
{
    static bool all(const void*, void* ctx, CommandParamSink sink) noexcept
    {
        const CommandParam p{0, nullptr, nullptr, static_cast<ScalarType>(255)};
        return sink(ctx, p);
    }

    static bool at(const void* m, std::uint32_t i, void* ctx, CommandParamSink sink) noexcept
    {
        return i == 0 && all(m, ctx, sink);
    }

    inline static constexpr CommandParamOps ops{1, &all, &at};

    static constexpr Command make() noexcept
    {
        return Command{"invalid", nullptr, nullptr, nullptr, &ops};
    }
};
} // namespace telemetry::detail

template <class Emit>
void verify(std::uint32_t size, Emit emit)
{
    using namespace telemetry_resource::detail;
    BinaryWriter measure;
    CHECK(emit(measure) && measure.ok() && measure.count() == size);
    Bytes storage(size + 2, std::byte{0xa5});
    OutputWriter out{{storage.data() + 1, size}};
    CHECK(emit(out) && out.ok() && out.remaining() == 0 && !out.full());
    CHECK(storage.front() == std::byte{0xa5} && storage.back() == std::byte{0xa5});
}

int main()
{
    using namespace telemetry;
    using namespace telemetry_resource;
    namespace d = telemetry_resource::detail;
    const Scalar values[]{Scalar{},
                          Scalar::fromBool(true),
                          Scalar::fromU8(255),
                          Scalar::fromU16(65535),
                          Scalar::fromU32(UINT32_MAX),
                          Scalar::fromU64(UINT64_MAX),
                          Scalar::fromS8(INT8_MIN),
                          Scalar::fromS16(INT16_MIN),
                          Scalar::fromS32(INT32_MIN),
                          Scalar::fromS64(INT64_MIN),
                          Scalar::fromF32(-0.f),
                          Scalar::fromF64(0.1)};
    for (unsigned n = 0; n < 128; ++n)
    {
        const std::string name(n, static_cast<char>('a' + n % 26));
        const std::string unit(n % 13, '\n');
        for (const auto& value : values)
        {
            d::StringRef text;
            CHECK(d::stringRef(name.c_str(), text));
            std::uint32_t size;
            CHECK(d::wireSize(text, size));
            verify(size,
                   [&](auto& out) noexcept
                   {
                       return out.string(text.view());
                   });
            verify(d::wireSize(value),
                   [&](auto& out) noexcept
                   {
                       return out.scalar(value);
                   });
            CHECK(d::catalogPayloadSize(text, size));
            verify(size,
                   [&](auto& out) noexcept
                   {
                       return d::catalogPayload(out, 65535, 65536, text);
                   });
            CHECK(d::commandPayloadSize(text, size));
            verify(size,
                   [&](auto& out) noexcept
                   {
                       return d::commandPayload(out, 65535, 65535, Command{}, 0, text);
                   });

            const Field field{name.c_str(), unit.c_str(), value.type()};
            d::LabelRefs refs;
            CHECK(d::labelRefs(field.name, field.unit, false, refs) &&
                  d::fieldPayloadSize(field, refs, size));
            verify(size,
                   [&](auto& out) noexcept
                   {
                       return d::fieldPayload(out, 65535, 65535, field, 0, refs);
                   });
            const CommandParam parameter{UINT32_MAX, n % 2 ? nullptr : name.c_str(),
                                         n % 3 ? nullptr : unit.c_str(), value.type()};
            CHECK(d::labelRefs(parameter.name, parameter.unit, true, refs) &&
                  d::parameterPayloadSize(parameter, refs, size));
            verify(size,
                   [&](auto& out) noexcept
                   {
                       return d::parameterPayload(out, UINT32_MAX, parameter, 0, refs);
                   });
            // String views preserve embedded NUL; C-string label lengths do not.
            const char enumName[]{'A', '\0', static_cast<char>('0' + n % 10)};
            CHECK(d::stringRef(std::string_view{enumName, 3}, text));
            for (bool parameterEnum : {false, true})
            {
                CHECK(d::enumPayloadSize(parameterEnum, value, text, size));
                verify(size,
                       [&](auto& out) noexcept
                       {
                           return out.u32(UINT32_MAX) && (!parameterEnum || out.u32(UINT32_MAX)) &&
                                  out.u32(n) && out.scalar(value) && out.string(text.view());
                       });
            }
        }
    }
    d::StringRef text;
    CHECK(!d::stringRef(nullptr, text) && d::stringRef(nullptr, text, true) && text.size == 0);
    std::uint32_t size;
    CHECK(!d::wireSize(d::StringRef{"", UINT32_MAX}, size));
    CHECK(!d::catalogPayloadSize(d::StringRef{"", UINT32_MAX - 8}, size));
    CHECK(!d::commandPayloadSize(d::StringRef{"", UINT32_MAX - 20}, size));
    d::LabelRefs huge{{"", UINT32_MAX}, {"", 1}};
    const Field f{"", "", ScalarType::U64};
    CHECK(!d::fieldPayloadSize(f, huge, size));
    const CommandParam p{0, nullptr, nullptr, ScalarType::F64};
    CHECK(!d::parameterPayloadSize(p, huge, size));
    CHECK(!d::enumPayloadSize(true, Scalar::fromU64(1), {"", UINT32_MAX}, size));

    // Every unrecognized internal tag must reject the whole provider, never
    // become a valid reserved/Null field in schema or values.
    for (unsigned code = 0; code < 256; ++code)
    {
        const auto type = static_cast<ScalarType>(code);
        const bool valid = std::any_of(std::begin(values), std::end(values),
                                       [&](const Scalar& v)
                                       {
                                           return v.type() == type;
                                       });
        CHECK((toWireType(type) != invalidWireType) == valid);
        if (valid)
        {
            continue;
        }
        const Field rows[]{{"bad", "", type}};
        const Catalog catalogs[]{Catalog{"bad", rows}};
        SchemaFile schema{CatalogIndex{catalogs}};
        ValuesFile current{schema};
        CHECK(schema.size() == 0 && schema.read(0, {}).status == resource::Status::InvalidData);
        CHECK(current.size() == 0 && current.read(0, {}).status == resource::Status::InvalidData);
    }
    constexpr Command invalid =
        telemetry::detail::CommandBinding<&invalidParameterTarget, void,
                                          telemetry::detail::NoCommandArgs>::make();
    const CommandCatalog catalog[]{CommandCatalog{"bad", &invalid, 1}};
    CommandsFile commands{CommandCatalogIndex{catalog}};
    CHECK(commands.size() == 0 && commands.read(0, {}).status == resource::Status::InvalidData);
    std::printf("Metadata sizing and invalid tags: %u checks passed\n", checks);
}
