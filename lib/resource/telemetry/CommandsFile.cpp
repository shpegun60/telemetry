/**
 * @file CommandsFile.cpp
 * @brief Resumable command records via the public synchronous metadata visitor.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "CommandsFile.hpp"
#include "detail/BinaryStream.hpp"
#include "detail/Metadata.hpp"
#include <telemetry/command/TelemetryCommandCatalogIndex.h>

namespace telemetry_resource
{
namespace
{
using detail::BinaryWriter;

constexpr auto code(CommandRecord r) noexcept
{
    return static_cast<std::uint8_t>(r);
}

template <class Visit>
bool parameters(const telemetry::Command& command, Visit&& visit) noexcept
{
    return command.describe == nullptr || command.forEachParameter(std::forward<Visit>(visit));
}
} // namespace

CommandsFile::CommandsFile(const telemetry::CommandCatalogIndex& index,
                           telemetry::detail::CurrentAbiTag) noexcept
    : catalogs_(index.data()), count_(index.size())
{
    detail::MetadataMeasure measure{"TCMD"};
    for (const auto catalog : index.catalogs())
    {
        if (!measure.record(code(CommandRecord::Catalog),
                            [&](BinaryWriter& out) noexcept
                            {
                                return detail::catalogPayload(
                                    out, catalog.index(),
                                    static_cast<std::uint32_t>(catalog.commands().size()),
                                    catalog.name());
                            }))
        {
            return;
        }
        for (const auto entry : catalog.commands())
        {
            std::uint32_t parameterCount = 0;
            if (!parameters(
                    entry.command(),
                    [&](const telemetry::CommandParam& p) noexcept
                    {
                        // Generated descriptions are positional, including unnamed arguments.
                        if (p.index != parameterCount)
                        {
                            return false;
                        }
                        std::uint32_t enums = 0;
                        if (!detail::enumEntries(
                                p.type,
                                [&](const telemetry::Scalar& value, std::string_view name) noexcept
                                {
                                    const bool ok = measure.record(
                                        code(CommandRecord::ParameterEnum),
                                        [&](BinaryWriter& out) noexcept
                                        {
                                            return out.u32(entry.id()) && out.u32(parameterCount) &&
                                                   out.u32(enums) && out.scalar(value) &&
                                                   out.string(name);
                                        });
                                    if (ok)
                                    {
                                        ++enums;
                                        ++enums_;
                                    }
                                    return ok;
                                }))
                        {
                            return false;
                        }
                        if (!measure.record(code(CommandRecord::Parameter),
                                            [&](BinaryWriter& out) noexcept
                                            {
                                                return detail::parameterPayload(out, entry.id(), p,
                                                                                enums);
                                            }))
                        {
                            return false;
                        }
                        ++parameterCount;
                        ++parameters_;
                        return true;
                    }))
            {
                return;
            }
            if (!measure.record(code(CommandRecord::Command),
                                [&](BinaryWriter& out) noexcept
                                {
                                    return detail::commandPayload(out, catalog.index(),
                                                                  entry.index(), entry.command(),
                                                                  parameterCount);
                                }))
            {
                return;
            }
            ++commands_;
        }
    }
    hash_ = measure.hash.value();
    size_ = measure.size;
    records_ = measure.records;
}

resource::ReadResult CommandsFile::read(resource::Cursor cursor,
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
                              return out.raw("TCMD") && out.u16(binaryMajor) &&
                                     out.u16(binaryMinor) && out.u32(metadataHeaderSize) &&
                                     out.u32(size_) && out.u32(hash_) && out.u32(records_) &&
                                     out.u32(static_cast<std::uint32_t>(count_)) &&
                                     out.u32(commands_) && out.u32(parameters_) && out.u32(enums_);
                          }))
    {
        return stream.result();
    }
    for (const auto catalog : telemetry::CommandCatalogIndex{catalogs_, count_}.catalogs())
    {
        if (!stream.record(code(CommandRecord::Catalog),
                           [&](BinaryWriter& out) noexcept
                           {
                               return detail::catalogPayload(
                                   out, catalog.index(),
                                   static_cast<std::uint32_t>(catalog.commands().size()),
                                   catalog.name());
                           }))
        {
            return stream.result();
        }
        for (const auto entry : catalog.commands())
        {
            std::uint32_t count = 0;
            if (!parameters(entry.command(),
                            [&](const telemetry::CommandParam&) noexcept
                            {
                                if (count == UINT32_MAX)
                                {
                                    return false;
                                }
                                ++count;
                                return true;
                            }))
            {
                stream.fail(resource::Status::InvalidData);
                return stream.result();
            }
            if (!stream.record(code(CommandRecord::Command),
                               [&](BinaryWriter& out) noexcept
                               {
                                   return detail::commandPayload(
                                       out, catalog.index(), entry.index(), entry.command(), count);
                               }))
            {
                return stream.result();
            }
            if (!parameters(
                    entry.command(),
                    [&](const telemetry::CommandParam& p) noexcept
                    {
                        std::uint32_t enums = 0;
                        if (!detail::enumCount(p.type, enums))
                        {
                            return stream.fail(resource::Status::InvalidData);
                        }
                        if (!stream.record(code(CommandRecord::Parameter),
                                           [&](BinaryWriter& out) noexcept
                                           {
                                               return detail::parameterPayload(out, entry.id(), p,
                                                                               enums);
                                           }))
                        {
                            return false;
                        }
                        std::uint32_t ordinal = 0;
                        return detail::enumEntries(
                            p.type,
                            [&](const telemetry::Scalar& value, std::string_view name) noexcept
                            {
                                const bool ok = stream.record(
                                    code(CommandRecord::ParameterEnum),
                                    [&](BinaryWriter& out) noexcept
                                    {
                                        return out.u32(entry.id()) &&
                                               out.u32(static_cast<std::uint32_t>(p.index)) &&
                                               out.u32(ordinal) && out.scalar(value) &&
                                               out.string(name);
                                    });
                                ++ordinal;
                                return ok;
                            });
                    }))
            {
                return stream.result();
            }
        }
    }
    return stream.result();
}
} // namespace telemetry_resource
