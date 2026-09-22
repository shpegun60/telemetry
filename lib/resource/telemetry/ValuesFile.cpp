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
    const auto kind = static_cast<BlockKind>(cursor >> detail::kindShift);
    const auto key = static_cast<std::uint32_t>(cursor >> detail::keyShift);
    const auto offset = static_cast<std::uint32_t>(cursor & detail::offsetMask);
    if ((kind == BlockKind::Prefix && (key != 0 || offset > valuesHeaderSize)) ||
        (kind == BlockKind::End && (key != 0 || offset != 0)) || kind == BlockKind::Catalog ||
        (kind == BlockKind::Entry && offset != 0))
    {
        return {resource::Status::InvalidCursor, cursor};
    }
    if (kind == BlockKind::End)
    {
        return {resource::Status::Ok, detail::endCursor, 0, true};
    }

    const telemetry::CatalogIndex index{catalogs_, count_};
    output = output.first(std::min<std::size_t>(output.size(), UINT32_MAX));
    std::uint32_t used = 0;
    auto next = cursor;
    // Values contain fixed-width tokens, so they need no metadata encoder or
    // block-local traversal state. Only the header permits a partial copy.
    if (kind == BlockKind::Prefix)
    {
        std::byte header[valuesHeaderSize]{std::byte{'T'}, std::byte{'V'}, std::byte{'A'},
                                           std::byte{'L'}};
        detail::storePayload(header + 4, binaryMajor, 2);
        detail::storePayload(header + 6, binaryMinor, 2);
        detail::storePayload(header + 8, hash_.value(), 8);
        detail::storePayload(header + 16, fields_, 4);
        used = static_cast<std::uint32_t>(
            std::min<std::size_t>(valuesHeaderSize - offset, output.size()));
        if (used != 0)
        {
            std::memcpy(output.data(), header + offset, used);
        }
        if (used < valuesHeaderSize - offset)
        {
            return {used == 0 ? resource::Status::BufferTooSmall : resource::Status::Ok,
                    detail::pack(BlockKind::Prefix, 0, offset + used), used};
        }
        next = firstValue(index, 0);
    }
    while (next != detail::endCursor)
    {
        const auto id = static_cast<std::uint32_t>(next >> detail::keyShift);
        const auto* field = index.find(id);
        if (field == nullptr)
        {
            // As with metadata READ, the caller discards any copied prefix.
            return {resource::Status::InvalidCursor, cursor};
        }
        const auto width = 1u + payloadSize(toWireType(field->readType));
        if (width > output.size() - used)
        {
            return {used == 0 && next == cursor ? resource::Status::BufferTooSmall
                                                : resource::Status::Ok,
                    next, used};
        }
        // Reserve the complete token before invoking its live getter once.
        valueToken(output.subspan(used, width), *field);
        used += width;
        const auto group = id >> 16;
        const auto* catalog = index.catalog(static_cast<telemetry::GroupId>(group));
        next = (id & 0xffffu) + 1 < catalog->count ? detail::pack(BlockKind::Entry, id + 1)
                                                   : firstValue(index, group + 1);
    }
    return {resource::Status::Ok, detail::endCursor, used, true};
}
} // namespace telemetry_resource
