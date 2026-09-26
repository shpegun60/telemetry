// Review repro (resource slice): commandFlags.Reserved is derived from invoke == nullptr
// only (Metadata.hpp commandPayload). A descriptor with no invoker but a parameter table
// is served as "Reserved, parameterCount=1" plus a Parameter record, which
// web/telemetryBinary.js rejects ("reserved command has parameters").
// Same test-only friend specialization technique as tests/resources/MetadataSizeCheck.cpp.
#include <telemetry/Telemetry.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include <cstdio>
#include <vector>

telemetry::CommandResult target() noexcept { return telemetry::CommandResult::Failed; }

namespace telemetry::detail
{
template <>
struct CommandBinding<&target, void, NoCommandArgs>
{
    static bool all(const void*, void* ctx, CommandParamSink sink) noexcept
    {
        const CommandParam p{0, "p", "", ScalarType::F32};
        return sink(ctx, p);
    }
    static bool at(const void* m, std::uint32_t i, void* ctx, CommandParamSink sink) noexcept
    {
        return i == 0 && all(m, ctx, sink);
    }
    inline static constexpr CommandParamOps ops{1, &all, &at};
    static constexpr Command make() noexcept { return Command{"c", nullptr, nullptr, nullptr, &ops}; }
};
} // namespace telemetry::detail

static std::uint32_t u32(const std::vector<std::byte>& b, std::size_t p)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= std::uint32_t(std::to_integer<unsigned>(b[p + i])) << (8 * i);
    return v;
}

int main()
{
    static constexpr telemetry::Command command =
        telemetry::detail::CommandBinding<&target, void, telemetry::detail::NoCommandArgs>::make();
    const telemetry::CommandCatalog catalogs[]{telemetry::CommandCatalog{"g", &command, 1}};
    telemetry_resource::CommandsFile file{telemetry::CommandCatalogIndex{catalogs}};
    std::vector<std::byte> all(file.size());
    const auto r = file.read(0, all);
    std::printf("read status=%u eof=%u size=%u\n", unsigned(r.status), unsigned(r.eof), file.size());
    for (std::size_t at = 44; at < all.size(); at += 8 + u32(all, at + 4))
        if (std::to_integer<unsigned>(all[at]) == 2)
            std::printf("Command record: parameterCount=%u commandFlags=%u\n", u32(all, at + 20),
                        u32(all, at + 24));
    std::printf("header parameterCount=%u\n", u32(all, 36));
    return 0;
}
