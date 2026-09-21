/**
 * @file CommandsFile.cpp
 * @brief Resumable grouped command schemas through synchronous public visitors.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "CommandsFile.hpp"
#include "detail/Json.hpp"
#include <telemetry/serialization/TelemetryCommandJson.h>

namespace telemetry_resource
{
namespace
{
using detail::Stream;
using detail::Writer;

bool parameters(Writer& out, const telemetry::Command& command) noexcept
{
    if (command.describe == nullptr)
    {
        return true;
    }
    return command.forEachParameter(
        [&out](const telemetry::CommandParam& p) noexcept
        {
            if ((p.index != 0 && !out.text(",")) || !out.text("{\"i\":") || !out.integer(p.index))
            {
                return false;
            }
            if (p.name != nullptr && (!out.text(",\"n\":") || !out.requiredString(p.name)))
            {
                return false;
            }
            if (p.unit != nullptr && (!out.text(",\"u\":") || !out.requiredString(p.unit)))
            {
                return false;
            }
            return out.text(",\"t\":\"") && out.text(detail::typeName(p.type)) && out.text("\"") &&
                   detail::limits(out, p.type) && out.text("}");
        });
}

bool emit(const telemetry::CommandCatalogIndex& index, std::uint32_t hash, Stream& stream) noexcept
{
    if (!stream.record(
            [hash](Writer& out) noexcept
            {
                return out.text("{\"schema\":\"") && out.hex32(hash) &&
                       out.text("\",\"meta\":{\"formatVersion\":") &&
                       out.integer(telemetry::jsonSchemaFormatVersion) &&
                       out.text("},\"commandCatalogs\":[");
            }))
    {
        return false;
    }
    for (const auto catalog : index.catalogs())
    {
        const auto entries = catalog.commands();
        if (stream.skip(entries.size() + 2))
        {
            continue;
        }
        if (!stream.record(
                [&](Writer& out) noexcept
                {
                    return (catalog.index() == 0 || out.text(",")) && out.text("{\"id\":") &&
                           out.integer(catalog.index()) && out.text(",\"name\":") &&
                           out.requiredString(catalog.name()) && out.text(",\"commands\":[");
                }))
        {
            return false;
        }
        const auto start = stream.skipEntries(entries.size());
        for (std::size_t i = start; i < entries.size(); ++i)
        {
            const auto& command = catalog.catalog().commands[i];
            if (!stream.record(
                    [&](Writer& out) noexcept
                    {
                        const auto id = telemetry::makeId(catalog.index(),
                                                          static_cast<telemetry::EntryOffset>(i));
                        return (i == 0 || out.text(",")) && out.text("{\"i\":") && out.integer(i) &&
                               out.text(",\"id\":") && out.integer(id) && out.text(",\"n\":") &&
                               out.requiredString(command.name) && out.text(",\"params\":[") &&
                               parameters(out, command) && out.text("]}");
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

CommandsFile::CommandsFile(const telemetry::CommandCatalogIndex& index,
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

resource::ReadResult CommandsFile::read(resource::Cursor cursor,
                                        resource::Output out) const noexcept
{
    if (size_ == 0)
    {
        return {resource::Status::InvalidData, cursor};
    }
    Stream stream{cursor, out};
    (void)emit(telemetry::CommandCatalogIndex{catalogs_, count_}, hash_, stream);
    return stream.result(records_);
}
} // namespace telemetry_resource
