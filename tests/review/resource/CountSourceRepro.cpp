// Review repro (resource slice): SchemaFile construction and the fingerprint use the
// number of enum entries actually visited; SchemaFile::read writes EnumOps::count.
// Nothing checks that the two agree. Uses the same explicit enumType<> specialization
// technique as tests/resources/LocalityCheck.cpp (an ops table whose count says 2
// while its traversal emits 1 entry).
#include <telemetry/Telemetry.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include <cstdio>
#include <vector>

enum class Odd : std::uint16_t { Value };

struct OddOps
{
    static bool at(std::uint32_t i, void* context, telemetry::EnumEntrySink sink) noexcept
    {
        return i == 0 && sink != nullptr && sink(context, telemetry::Scalar::fromU16(0), "Value");
    }
    static bool all(void* context, telemetry::EnumEntrySink sink) noexcept
    {
        return at(0, context, sink);
    }
    inline static constexpr telemetry::EnumOps ops{2, &all, &at}; // count disagrees
};

namespace telemetry
{
template <>
constexpr FieldType enumType<Odd>() noexcept
{
    return FieldType{ScalarType::U16, &OddOps::ops}.withLimits(0, 0, 0);
}
} // namespace telemetry

Odd readOdd() noexcept { return Odd::Value; }

static std::uint32_t u32(const std::vector<std::byte>& b, std::size_t p)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= std::uint32_t(std::to_integer<unsigned>(b[p + i])) << (8 * i);
    return v;
}

int main()
{
    using namespace telemetry;
    static constexpr FieldTable rows{field<&readOdd>("odd", "")};
    const Catalog catalogs[]{Catalog{"g", rows.data(), rows.size()}};
    telemetry_resource::SchemaFile schema{CatalogIndex{catalogs}};
    std::vector<std::byte> all(schema.size());
    const auto r = schema.read(0, all);
    std::printf("read status=%u written=%u eof=%u size=%u\n", unsigned(r.status), r.written,
                unsigned(r.eof), schema.size());
    // Header: enumEntryCount at offset 36. Walk records; Field enumCount is payload +16.
    std::printf("header enumEntryCount=%u\n", u32(all, 36));
    unsigned enumRecords = 0, fieldEnumCount = 0;
    for (std::size_t at = 44; at < all.size(); at += 8 + u32(all, at + 4))
    {
        const auto type = std::to_integer<unsigned>(all[at]);
        if (type == 3)
            fieldEnumCount = u32(all, at + 8 + 16);
        if (type == 4)
            ++enumRecords;
    }
    std::printf("Field record enumCount=%u, enum records on the wire=%u\n", fieldEnumCount,
                enumRecords);
    const bool defect = r.status == resource::Status::Ok && fieldEnumCount != enumRecords;
    std::puts(defect ? "DEFECT: provider serves Ok bytes whose Field.enumCount disagrees with "
                       "its enum records and with the hashed value (reference decoder: "
                       "'incomplete schema')"
                     : "no defect");
    return defect ? 1 : 0;
}
