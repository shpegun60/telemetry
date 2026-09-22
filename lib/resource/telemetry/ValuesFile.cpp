/**
 * @file ValuesFile.cpp
 * @brief Atomic status plus little-endian payload, with no per-value type or ID.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "ValuesFile.hpp"
#include "detail/BlockStream.hpp"
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

namespace
{
// Values omit empty catalogs. This scans only consecutive empty groups AFTER
// the current entry (or after the header), never groups preceding a resume key.
resource::Cursor firstValue(const telemetry::CatalogIndex& index, std::uint32_t group) noexcept
{
    while (group < index.size())
    {
        if (index.catalog(static_cast<telemetry::GroupId>(group))->count != 0)
        {
            return detail::pack(detail::BlockKind::Entry, group << 16);
        }
        ++group;
    }
    return detail::endCursor;
}
} // namespace

resource::ReadResult ValuesFile::read(resource::Cursor cursor,
                                      resource::Output output) const noexcept
{
    if (size_ == 0)
    {
        return {resource::Status::InvalidData, cursor};
    }
    using detail::BlockKind;
    detail::BlockStream stream{cursor, output};
    const telemetry::CatalogIndex index{catalogs_, count_};
    while (stream.active())
    {
        if (stream.kind() == BlockKind::Prefix)
        {
            std::byte header[valuesHeaderSize]{std::byte{'T'}, std::byte{'V'}, std::byte{'A'},
                                               std::byte{'L'}};
            detail::storePayload(header + 4, binaryMajor, 2);
            detail::storePayload(header + 6, binaryMinor, 2);
            detail::storePayload(header + 8, hash_.value(), 8);
            detail::storePayload(header + 16, fields_, 4);
            if (!stream.fixedRecord(header))
            {
                break;
            }
            stream.finish(firstValue(index, 0));
        }
        else if (stream.kind() == BlockKind::Entry)
        {
            const auto id = stream.key();
            const auto* field = index.find(id);
            if (field == nullptr)
            {
                stream.fail(resource::Status::InvalidCursor);
                break;
            }
            const auto width = 1u + payloadSize(toWireType(field->readType));
            // Preflight is inside atomic(): no getter runs for a partial token.
            if (!stream.atomic(width,
                               [&](resource::Output out) noexcept
                               {
                                   valueToken(out, *field);
                               }))
            {
                break;
            }
            const auto group = id >> 16;
            const auto position = id & 0xffffu;
            const auto* catalog = index.catalog(static_cast<telemetry::GroupId>(group));
            stream.finish(position + 1 < catalog->count ? detail::pack(BlockKind::Entry, id + 1)
                                                        : firstValue(index, group + 1));
        }
        else
        {
            // Catalog cursors have no meaning in a values stream.
            stream.fail(resource::Status::InvalidCursor);
        }
    }
    return stream.result();
}
} // namespace telemetry_resource
