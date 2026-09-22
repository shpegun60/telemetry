/**
 * @file Metadata.hpp
 * @brief Shared record payload encoders and synchronous enum traversal.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "BinaryWriter.hpp"
#include <telemetry/field/TelemetryField.h>
#include <telemetry/command/TelemetryCommand.h>

namespace telemetry_resource::detail
{
template <class Visit>
bool enumEntries(const telemetry::FieldType& type, Visit&& visit) noexcept
{
    if (!type.hasEnum())
    {
        return true;
    }
    using V = std::remove_reference_t<Visit>;
    return type.describeEnum(
        &visit,
        [](void* context, const telemetry::Scalar& code, std::string_view name) noexcept
        {
            return (*static_cast<V*>(context))(code, name);
        });
}

inline bool enumCount(const telemetry::FieldType& type, std::uint32_t& count) noexcept
{
    count = 0;
    return enumEntries(type,
                       [&](const telemetry::Scalar&, std::string_view) noexcept
                       {
                           if (count == UINT32_MAX)
                           {
                               return false;
                           }
                           ++count;
                           return true;
                       });
}

inline bool requiredString(BinaryWriter& out, const char* text) noexcept
{
    return text != nullptr ? out.string(text) : out.fail();
}

inline bool labels(BinaryWriter& out, const char* name, const char* unit, bool nullable) noexcept
{
    if (!nullable && (name == nullptr || unit == nullptr))
    {
        return out.fail();
    }
    const std::string_view n = name != nullptr ? name : "";
    const std::string_view u = unit != nullptr ? unit : "";
    if (n.size() > UINT32_MAX || u.size() > UINT32_MAX)
    {
        return out.fail();
    }
    return out.u32(static_cast<std::uint32_t>(n.size())) &&
           out.u32(static_cast<std::uint32_t>(u.size())) && out.raw(n) && out.raw(u);
}

inline bool limits(BinaryWriter& out, const telemetry::FieldType& type) noexcept
{
    return out.scalar(type.minimum()) && out.scalar(type.maximum()) &&
           out.scalar(type.defaultValue());
}

inline bool catalogPayload(BinaryWriter& out, std::uint32_t index, std::uint32_t count,
                           const char* name) noexcept
{
    return out.u32(index) && out.u32(count) && requiredString(out, name);
}

inline bool fieldPayload(BinaryWriter& out, std::uint32_t group, std::uint32_t position,
                         const telemetry::Field& field, std::uint32_t enums) noexcept
{
    const auto access = (field.get ? Readable : 0) | (field.writable() ? Writable : 0);
    const bool reserved = field.readType == telemetry::ScalarType::Null && access == 0;
    return out.u32(group) && out.u32(position) && out.u32((group << 16) | position) &&
           out.u32(field.flags().value()) && out.u32(enums) &&
           out.u8(static_cast<std::uint8_t>(toWireType(field.declaredType))) &&
           out.u8(static_cast<std::uint8_t>(toWireType(field.readType))) &&
           out.u8(static_cast<std::uint8_t>(access)) && out.u8(reserved ? Reserved : 0) &&
           labels(out, field.name, field.unit, false) && limits(out, field.declaredType);
}

inline bool parameterPayload(BinaryWriter& out, std::uint32_t commandId,
                             const telemetry::CommandParam& p, std::uint32_t enums) noexcept
{
    if (p.index > UINT32_MAX)
    {
        return out.fail();
    }
    const auto presence = (p.name != nullptr ? HasName : 0) | (p.unit != nullptr ? HasUnit : 0);
    return out.u32(commandId) && out.u32(static_cast<std::uint32_t>(p.index)) && out.u32(0) &&
           out.u32(enums) && out.u8(static_cast<std::uint8_t>(toWireType(p.type))) &&
           out.u8(static_cast<std::uint8_t>(presence)) && out.u16(0) &&
           labels(out, p.name, p.unit, true) && limits(out, p.type);
}

inline bool commandPayload(BinaryWriter& out, std::uint32_t group, std::uint32_t position,
                           const telemetry::Command& command, std::uint32_t parameters) noexcept
{
    return out.u32(group) && out.u32(position) && out.u32((group << 16) | position) &&
           out.u32(parameters) && out.u32(0) && requiredString(out, command.name);
}
} // namespace telemetry_resource::detail
