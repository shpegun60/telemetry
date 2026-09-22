/**
 * @file CommandsFile.cpp
 * @brief Resumable command records via the public synchronous metadata visitor.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "CommandsFile.hpp"
#include "detail/BlockStream.hpp"
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
    return !command.hasDescription() || command.forEachParameter(std::forward<Visit>(visit));
}
} // namespace

CommandsFile::CommandsFile(const telemetry::CommandCatalogIndex& index,
                           telemetry::detail::CurrentAbiTag) noexcept
    : catalogs_(index.data()), count_(index.size())
{
    detail::MetadataMeasure measure{"TCMD"};
    for (const auto catalog : index.catalogs())
    {
        const auto catalogStart = measure.size;
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
        if (measure.size - catalogStart > detail::offsetMask)
        {
            return;
        }
        for (const auto entry : catalog.commands())
        {
            const auto blockStart = measure.size;
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
            if (measure.size - blockStart > detail::offsetMask)
            {
                return;
            }
            ++commands_;
        }
    }
    hash_ = measure.hash;
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
    using detail::BlockKind;
    detail::BlockStream stream{cursor, output};
    const telemetry::CommandCatalogIndex index{catalogs_, count_};
    while (stream.active())
    {
        if (stream.kind() == BlockKind::Prefix)
        {
            if (!stream.rawRecord(metadataHeaderSize,
                                  [&](BinaryWriter& out) noexcept
                                  {
                                      return out.raw("TCMD") && out.u16(binaryMajor) &&
                                             out.u16(binaryMinor) && out.u32(metadataHeaderSize) &&
                                             out.u32(size_) && out.u64(hash_.value()) &&
                                             out.u32(records_) &&
                                             out.u32(static_cast<std::uint32_t>(count_)) &&
                                             out.u32(commands_) && out.u32(parameters_) &&
                                             out.u32(enums_);
                                  }))
            {
                break;
            }
            stream.finish(detail::catalogCursor(0, count_));
        }
        else if (stream.kind() == BlockKind::Catalog)
        {
            const auto group = stream.key();
            if (group >= count_)
            {
                stream.fail(resource::Status::InvalidCursor);
                break;
            }
            const auto& catalog = *index.catalog(static_cast<telemetry::GroupId>(group));
            if (!stream.record(code(CommandRecord::Catalog),
                               [&](BinaryWriter& out) noexcept
                               {
                                   return detail::catalogPayload(
                                       out, group, static_cast<std::uint32_t>(catalog.count),
                                       catalog.name);
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
            const auto* command = index.find(id);
            if (command == nullptr)
            {
                stream.fail(resource::Status::InvalidCursor);
                break;
            }
            if (!stream.record(code(CommandRecord::Command),
                               [&](BinaryWriter& out) noexcept
                               {
                                   return detail::commandPayload(out, id >> 16, id & 0xffffu,
                                                                 *command,
                                                                 command->parameterCount());
                               }))
            {
                break;
            }
            // Only the selected command is traversed. Its original sequential
            // callback avoids indexed dispatch for every parameter in this block.
            if (!parameters(
                    *command,
                    [&](const telemetry::CommandParam& p) noexcept
                    {
                        if (!stream.record(code(CommandRecord::Parameter),
                                           [&](BinaryWriter& out) noexcept
                                           {
                                               return detail::parameterPayload(out, id, p,
                                                                               p.type.enumCount());
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
                                        return out.u32(id) &&
                                               out.u32(static_cast<std::uint32_t>(p.index)) &&
                                               out.u32(ordinal) && out.scalar(value) &&
                                               out.string(name);
                                    });
                                ++ordinal;
                                return ok;
                            });
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
