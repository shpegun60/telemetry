/**
 * @file SchemaFile.cpp
 * @brief Resume field schema records without materializing the file.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "SchemaFile.hpp"
#include "detail/Json.hpp"
#include <telemetry/serialization/TelemetryJson.h>
#include <magic_enum/magic_enum.hpp>

namespace telemetry_resource
{
namespace
{
using detail::Stream;
using detail::Writer;

bool emit(const telemetry::CatalogIndex& index, std::uint32_t hash, Stream& stream) noexcept
{
    if (!stream.record(
            [hash](Writer& out) noexcept
            {
                if (!out.text("{\"schema\":\"") || !out.hex32(hash) ||
                    !out.text("\",\"meta\":{\"formatVersion\":") ||
                    !out.integer(telemetry::jsonSchemaFormatVersion) ||
                    !out.text(",\"fieldFlags\":{\"type\":\"u32\",\"values\":{"))
                {
                    return false;
                }
                constexpr auto flags =
                    magic_enum::enum_entries<telemetry::FieldFlag, magic_enum::as_flags<>>();
                bool first = true;
                for (const auto& [flag, name] : flags)
                {
                    if ((!first && !out.text(",")) || !out.text("\"") ||
                        !out.integer(static_cast<std::uint32_t>(flag)) || !out.text("\":") ||
                        !out.string(name))
                    {
                        return false;
                    }
                    first = false;
                }
                return out.text("}}},\"catalogs\":[");
            }))
    {
        return false;
    }
    for (const auto catalog : index.catalogs())
    {
        const auto entries = catalog.fields();
        if (stream.skip(entries.size() + 2))
        {
            continue;
        }
        if (!stream.record(
                [&](Writer& out) noexcept
                {
                    return (catalog.index() == 0 || out.text(",")) && out.text("{\"id\":") &&
                           out.integer(catalog.index()) && out.text(",\"name\":") &&
                           out.requiredString(catalog.name()) && out.text(",\"fields\":[");
                }))
        {
            return false;
        }
        const auto start = stream.skipEntries(entries.size());
        // Direct positional access after range validation avoids walking all
        // earlier fields when resuming a later record in a large catalog.
        for (std::size_t i = start; i < entries.size(); ++i)
        {
            const auto& field = catalog.catalog().fields[i];
            if (!stream.record(
                    [&](Writer& out) noexcept
                    {
                        const auto id = telemetry::makeId(catalog.index(),
                                                          static_cast<telemetry::EntryOffset>(i));
                        return (i == 0 || out.text(",")) && out.text("{\"i\":") && out.integer(i) &&
                               out.text(",\"id\":") && out.integer(id) && out.text(",\"n\":") &&
                               out.requiredString(field.name) && out.text(",\"u\":") &&
                               out.requiredString(field.unit) && out.text(",\"t\":\"") &&
                               out.text(detail::typeName(field.declaredType)) &&
                               out.text("\",\"w\":") &&
                               out.text(field.writable() ? "true" : "false") &&
                               out.text(",\"f\":") && out.integer(field.flags().value()) &&
                               detail::limits(out, field.declaredType) && out.text("}");
                    }))
            {
                return false;
            }
        }
        if (!stream.record(
                [](Writer& out) noexcept
                {
                    return out.text("]}");
                }))
        {
            return false;
        }
    }
    return stream.record(
        [](Writer& out) noexcept
        {
            return out.text("]}");
        });
}
} // namespace

SchemaFile::SchemaFile(const telemetry::CatalogIndex& index,
                       telemetry::detail::CurrentAbiTag) noexcept
    : catalogs_(index.data()), count_(index.size()), hash_(telemetry::schemaCrc(index))
{
    Stream measure;
    if (emit(index, hash_, measure))
    {
        size_ = measure.size();
        records_ = measure.records();
    }
}

resource::ReadResult SchemaFile::read(resource::Cursor cursor, resource::Output out) const noexcept
{
    if (size_ == 0)
    {
        return {resource::Status::InvalidData, cursor};
    }
    Stream stream{cursor, out};
    (void)emit(telemetry::CatalogIndex{catalogs_, count_}, hash_, stream);
    return stream.result(records_);
}
} // namespace telemetry_resource
