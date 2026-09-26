/**
 * @file SchemaFile.cpp
 * @brief Parent-first binary records and a single construction-time metadata pass.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "SchemaFile.hpp"
#include "detail/BlockStream.hpp"
#include "detail/Metadata.hpp"
#include <telemetry/catalog/TelemetryIndex.h>
#include <magic_enum/magic_enum.hpp>

namespace telemetry_resource
{
namespace
{
using detail::BinaryWriter;
constexpr auto flagDefinitions =
    magic_enum::enum_entries<telemetry::FieldFlag, magic_enum::as_flags<>>();

constexpr auto code(SchemaRecord r) noexcept
{
    return static_cast<std::uint8_t>(r);
}
} // namespace

SchemaFile::SchemaFile(const telemetry::CatalogIndex& index,
                       telemetry::detail::CurrentAbiTag) noexcept
    : catalogs_(index.data()), count_(index.size())
{
    detail::MetadataMeasure measure{"TSCH"};
    std::uint32_t valuesSize = valuesHeaderSize;
    for (const auto& [flag, name] : flagDefinitions)
    {
        if (!measure.record(code(SchemaRecord::FieldFlagDefinition),
                            [&](BinaryWriter& out) noexcept
                            {
                                return out.u32(static_cast<std::uint32_t>(flag)) &&
                                       out.string(name);
                            }))
        {
            return;
        }
    }
    if (measure.size > detail::offsetMask)
    {
        return;
    }
    for (const auto catalog : index.catalogs())
    {
        const auto catalogStart = measure.size;
        if (!measure.record(code(SchemaRecord::Catalog),
                            [&](BinaryWriter& out) noexcept
                            {
                                return detail::catalogPayload(
                                    out, catalog.index(),
                                    static_cast<std::uint32_t>(catalog.fields().size()),
                                    catalog.name());
                            }))
        {
            return;
        }
        if (measure.size - catalogStart > detail::offsetMask)
        {
            return;
        }
        for (const auto entry : catalog.fields())
        {
            const auto blockStart = measure.size;
            const auto& field = entry.field();
            std::uint32_t enums = 0;
            if (!detail::enumEntries(
                    field.declaredType,
                    [&](const telemetry::Scalar& value, std::string_view name) noexcept
                    {
                        const bool ok =
                            measure.record(code(SchemaRecord::FieldEnumEntry),
                                           [&](BinaryWriter& out) noexcept
                                           {
                                               return out.u32(entry.id()) && out.u32(enums) &&
                                                      out.scalar(value) && out.string(name);
                                           });
                        if (ok)
                        {
                            ++enums;
                            ++enums_;
                        }
                        return ok;
                    }) || enums != field.declaredType.enumCount())
            {
                return;
            }
            if (!measure.record(code(SchemaRecord::Field),
                                [&](BinaryWriter& out) noexcept
                                {
                                    return detail::fieldPayload(out, catalog.index(), entry.index(),
                                                                field, enums);
                                }))
            {
                return;
            }
            if (measure.size - blockStart > detail::offsetMask)
            {
                return;
            }
            ++fields_;
            const auto width = 1u + payloadSize(toWireType(field.readType));
            if (width > UINT32_MAX - valuesSize)
            {
                return;
            }
            valuesSize += width;
        }
    }
    hash_ = measure.hash;
    size_ = measure.size;
    records_ = measure.records;
    valuesSize_ = valuesSize;
}

resource::ReadResult SchemaFile::read(resource::Cursor cursor,
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
            if (!stream.rawRecord(
                    metadataHeaderSize,
                    [&](detail::OutputWriter& out) noexcept
                    {
                        return out.raw("TSCH") && out.u16(binaryMajor) && out.u16(binaryMinor) &&
                               out.u32(metadataHeaderSize) && out.u32(size_) &&
                               out.u64(hash_.value()) && out.u32(records_) &&
                               out.u32(static_cast<std::uint32_t>(count_)) && out.u32(fields_) &&
                               out.u32(enums_) &&
                               out.u32(static_cast<std::uint32_t>(flagDefinitions.size()));
                    }))
            {
                break;
            }
            for (const auto& [flag, name] : flagDefinitions)
            {
                detail::StringRef text;
                std::uint32_t size;
                if (!detail::stringRef(name, text) || !detail::namedPayloadSize(4, text, size))
                {
                    stream.fail(resource::Status::InvalidData);
                    return stream.result();
                }
                if (!stream.record(code(SchemaRecord::FieldFlagDefinition), size,
                                   [&](detail::OutputWriter& out) noexcept
                                   {
                                       return out.u32(static_cast<std::uint32_t>(flag)) &&
                                              out.string(text.view());
                                   }))
                {
                    return stream.result();
                }
            }
            stream.finish(detail::catalogCursor(0, count_));
        }
        else if (stream.kind() == BlockKind::Catalog)
        {
            // Validate the full 32-bit key before narrowing it to GroupId.
            const auto group = stream.key();
            if (group >= count_)
            {
                stream.fail(resource::Status::InvalidCursor);
                break;
            }
            const auto& catalog = *index.catalog(static_cast<telemetry::GroupId>(group));
            detail::StringRef name;
            std::uint32_t catalogSize;
            if (!detail::stringRef(catalog.name, name) ||
                !detail::catalogPayloadSize(name, catalogSize))
            {
                stream.fail(resource::Status::InvalidData);
                break;
            }
            if (!stream.record(code(SchemaRecord::Catalog), catalogSize,
                               [&](detail::OutputWriter& out) noexcept
                               {
                                   return detail::catalogPayload(
                                       out, group, static_cast<std::uint32_t>(catalog.count), name);
                               }))
            {
                break;
            }
            stream.finish(catalog.count != 0 ? detail::pack(BlockKind::Entry, group << 16)
                                             : detail::catalogCursor(group + 1, count_));
        }
        else
        {
            const auto id = stream.key();
            const auto* field = index.find(id);
            if (field == nullptr)
            {
                stream.fail(resource::Status::InvalidCursor);
                break;
            }
            {
                detail::LabelRefs refs;
                std::uint32_t fieldSize;
                if (!detail::labelRefs(field->name, field->unit, false, refs) ||
                    !detail::fieldPayloadSize(*field, refs, fieldSize))
                {
                    stream.fail(resource::Status::InvalidData);
                    break;
                }
                if (!stream.record(code(SchemaRecord::Field), fieldSize,
                                   [&](detail::OutputWriter& out) noexcept
                                   {
                                       return detail::fieldPayload(
                                           out, id >> 16, id & 0xffffu, *field,
                                           field->declaredType.enumCount(), refs);
                                   }))
                {
                    break;
                }
            }
            std::uint32_t ordinal = 0;
            if (!detail::enumEntries(
                    field->declaredType,
                    [&](const telemetry::Scalar& value, std::string_view name) noexcept
                    {
                        detail::StringRef text;
                        std::uint32_t size;
                        if (!detail::stringRef(name, text) ||
                            !detail::enumPayloadSize(false, value, text, size))
                        {
                            return stream.fail(resource::Status::InvalidData);
                        }
                        const bool ok = stream.record(code(SchemaRecord::FieldEnumEntry), size,
                                                      [&](detail::OutputWriter& out) noexcept
                                                      {
                                                          return out.u32(id) && out.u32(ordinal) &&
                                                                 out.scalar(value) &&
                                                                 out.string(text.view());
                                                      });
                        ++ordinal;
                        return ok;
                    }))
            {
                break;
            }
            stream.finish(detail::nextEntry(index, id));
        }
    }
    return stream.result();
}
} // namespace telemetry_resource
