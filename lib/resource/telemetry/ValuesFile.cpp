/**
 * @file ValuesFile.cpp
 * @brief Atomic status plus little-endian payload, with no per-value type or ID.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "ValuesFile.hpp"
#include "detail/BinaryStream.hpp"
#include <telemetry/catalog/TelemetryIndex.h>

namespace telemetry_resource
{
namespace
{
// Enter only after the entire destination token has been reserved. Keeping
// scalar normalization here lets it reuse a short-lived frame between fields.
void valueToken(resource::Output out, const telemetry::Field& field) noexcept
{
    const auto value = field.read();
    const bool available = value.type() != telemetry::ScalarType::Null;
    out[0] = static_cast<std::byte>(available ? ValueStatus::Available : ValueStatus::Unavailable);
    detail::storePayload(out.data() + 1, available ? detail::scalarBits(value) : 0,
                         static_cast<unsigned>(out.size() - 1));
}
} // namespace

ValuesFile::ValuesFile(const telemetry::CatalogIndex& index,
                       telemetry::detail::CurrentAbiTag tag) noexcept
    : ValuesFile(SchemaFile{index, tag})
{
}

ValuesFile::ValuesFile(const SchemaFile& schema) noexcept
    : catalogs_(schema.catalogs_), count_(schema.count_), size_(schema.valuesSize_),
      fields_(schema.fields_), hash_(schema.hash_)
{
}

resource::ReadResult ValuesFile::read(resource::Cursor cursor,
                                      resource::Output output) const noexcept
{
    if (size_ == 0)
    {
        return {resource::Status::InvalidData, cursor};
    }
    detail::BinaryStream stream{cursor, output, fields_ + 1};
    if (!stream.active())
    {
        return stream.result();
    }
    if (stream.skipEntries(1) == 0)
    {
        std::byte header[valuesHeaderSize]{std::byte{'T'}, std::byte{'V'}, std::byte{'A'},
                                           std::byte{'L'}};
        detail::storePayload(header + 4, binaryMajor, 2);
        detail::storePayload(header + 6, binaryMinor, 2);
        detail::storePayload(header + 8, hash_.value(), 8);
        detail::storePayload(header + 16, fields_, 4);
        if (!stream.fixedRecord(header))
        {
            return stream.result();
        }
    }
    for (const auto catalog : telemetry::CatalogIndex{catalogs_, count_}.catalogs())
    {
        const auto entries = catalog.fields();
        const auto first = stream.skipEntries(entries.size());
        for (std::size_t i = first; i < entries.size(); ++i)
        {
            const auto& field = catalog.catalog().fields[i];
            const auto width = payloadSize(toWireType(field.readType));
            if (!stream.atomic(1u + width,
                               [&](resource::Output out) noexcept
                               {
                                   valueToken(out, field);
                               }))
            {
                return stream.result();
            }
        }
    }
    return stream.result();
}
} // namespace telemetry_resource
