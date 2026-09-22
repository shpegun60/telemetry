/**
 * @file SchemaFile.cpp
 * @brief Parent-first binary records and a single construction-time metadata pass.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "SchemaFile.hpp"
#include "detail/BinaryStream.hpp"
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
    for (const auto catalog : index.catalogs())
    {
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
        for (const auto entry : catalog.fields())
        {
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
                    }))
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
    detail::BinaryStream stream{cursor, output, records_ + 1};
    if (!stream.rawRecord(metadataHeaderSize,
                          [&](BinaryWriter& out) noexcept
                          {
                              return out.raw("TSCH") && out.u16(binaryMajor) &&
                                     out.u16(binaryMinor) && out.u32(metadataHeaderSize) &&
                                     out.u32(size_) && out.u64(hash_.value()) &&
                                     out.u32(records_) &&
                                     out.u32(static_cast<std::uint32_t>(count_)) &&
                                     out.u32(fields_) && out.u32(enums_) &&
                                     out.u32(static_cast<std::uint32_t>(flagDefinitions.size()));
                          }))
    {
        return stream.result();
    }
    for (const auto& [flag, name] : flagDefinitions)
    {
        if (!stream.record(code(SchemaRecord::FieldFlagDefinition),
                           [&](BinaryWriter& out) noexcept
                           {
                               return out.u32(static_cast<std::uint32_t>(flag)) && out.string(name);
                           }))
        {
            return stream.result();
        }
    }
    for (const auto catalog : telemetry::CatalogIndex{catalogs_, count_}.catalogs())
    {
        if (!stream.record(code(SchemaRecord::Catalog),
                           [&](BinaryWriter& out) noexcept
                           {
                               return detail::catalogPayload(
                                   out, catalog.index(),
                                   static_cast<std::uint32_t>(catalog.fields().size()),
                                   catalog.name());
                           }))
        {
            return stream.result();
        }
        for (const auto entry : catalog.fields())
        {
            const auto& field = entry.field();
            std::uint32_t enums = 0;
            if (!detail::enumCount(field.declaredType, enums))
            {
                stream.fail(resource::Status::InvalidData);
                return stream.result();
            }
            if (!stream.record(code(SchemaRecord::Field),
                               [&](BinaryWriter& out) noexcept
                               {
                                   return detail::fieldPayload(out, catalog.index(), entry.index(),
                                                               field, enums);
                               }))
            {
                return stream.result();
            }
            std::uint32_t ordinal = 0;
            if (!detail::enumEntries(
                    field.declaredType,
                    [&](const telemetry::Scalar& value, std::string_view name) noexcept
                    {
                        const bool ok =
                            stream.record(code(SchemaRecord::FieldEnumEntry),
                                          [&](BinaryWriter& out) noexcept
                                          {
                                              return out.u32(entry.id()) && out.u32(ordinal) &&
                                                     out.scalar(value) && out.string(name);
                                          });
                        ++ordinal;
                        return ok;
                    }))
            {
                return stream.result();
            }
        }
    }
    return stream.result();
}
} // namespace telemetry_resource
