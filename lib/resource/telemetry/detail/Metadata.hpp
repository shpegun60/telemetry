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

// Lengths are discovered once per processed record and reused by arithmetic
// sizing and writing. No C-string scans occur inside the payload encoders.
struct StringRef
{
    const char* data = "";
    std::uint32_t size = 0;

    std::string_view view() const noexcept
    {
        return {data, size};
    }
};

struct LabelRefs
{
    StringRef name, unit;
};

inline bool stringRef(std::string_view text, StringRef& result) noexcept
{
    if (text.size() > UINT32_MAX)
    {
        return false;
    }
    result = {text.data(), static_cast<std::uint32_t>(text.size())};
    return true;
}

inline bool stringRef(const char* text, StringRef& result, bool nullable = false) noexcept
{
    if (text == nullptr && !nullable)
    {
        return false;
    }
    return stringRef(std::string_view{text != nullptr ? text : ""}, result);
}

inline bool labelRefs(const char* name, const char* unit, bool nullable, LabelRefs& refs) noexcept
{
    return stringRef(name, refs.name, nullable) && stringRef(unit, refs.unit, nullable);
}

constexpr bool addSize(std::uint32_t& size, std::uint32_t amount) noexcept
{
    if (amount > UINT32_MAX - size)
    {
        return false;
    }
    size += amount;
    return true;
}

constexpr bool wireSize(StringRef text, std::uint32_t& size) noexcept
{
    size = 4;
    return addSize(size, text.size);
}

inline std::uint32_t wireSize(const telemetry::Scalar& value) noexcept
{
    return 3u + payloadSize(toWireType(value.type()));
}

inline bool validType(const telemetry::FieldType& type) noexcept
{
    return toWireType(type) != invalidWireType;
}

inline bool namedPayloadSize(std::uint32_t fixed, StringRef name, std::uint32_t& size) noexcept
{
    size = fixed;
    return addSize(size, 4) && addSize(size, name.size);
}

inline bool catalogPayloadSize(StringRef name, std::uint32_t& size) noexcept
{
    return namedPayloadSize(8, name, size);
}

inline bool commandPayloadSize(StringRef name, std::uint32_t& size) noexcept
{
    return namedPayloadSize(20, name, size);
}

inline bool fieldPayloadSize(const telemetry::Field& field, const LabelRefs& refs,
                             std::uint32_t& size) noexcept
{
    if (!validType(field.declaredType) || toWireType(field.readType) == invalidWireType)
    {
        return false;
    }
    // All three metadata Scalars have the declared type by FieldType's invariant.
    size = 32 + 3 * (3u + payloadSize(toWireType(field.declaredType)));
    return addSize(size, refs.name.size) && addSize(size, refs.unit.size);
}

inline bool parameterPayloadSize(const telemetry::CommandParam& parameter, const LabelRefs& refs,
                                 std::uint32_t& size) noexcept
{
    if (parameter.index > UINT32_MAX || !validType(parameter.type))
    {
        return false;
    }
    size = 28 + 3 * (3u + payloadSize(toWireType(parameter.type)));
    return addSize(size, refs.name.size) && addSize(size, refs.unit.size);
}

inline bool enumPayloadSize(bool parameter, const telemetry::Scalar& code, StringRef name,
                            std::uint32_t& size) noexcept
{
    size = (parameter ? 12u : 8u) + wireSize(code);
    return addSize(size, 4) && addSize(size, name.size);
}

template <class Writer>
inline bool labels(Writer& out, const LabelRefs& refs) noexcept
{
    return out.u32(refs.name.size) && out.u32(refs.unit.size) && out.raw(refs.name.view()) &&
           out.raw(refs.unit.view());
}

template <class Writer>
inline bool limits(Writer& out, const telemetry::FieldType& type) noexcept
{
    // End each Scalar temporary's lifetime before constructing the next one.
    if (!out.scalar(type.minimum()))
    {
        return false;
    }
    if (!out.scalar(type.maximum()))
    {
        return false;
    }
    return out.scalar(type.defaultValue());
}

template <class Writer>
inline bool catalogPayload(Writer& out, std::uint32_t index, std::uint32_t count,
                           StringRef name) noexcept
{
    return out.u32(index) && out.u32(count) && out.string(name.view());
}

template <class Writer>
inline bool fieldPayload(Writer& out, std::uint32_t group, std::uint32_t position,
                         const telemetry::Field& field, std::uint32_t enums,
                         const LabelRefs& refs) noexcept
{
    const auto access = (field.get ? Readable : 0) | (field.writable() ? Writable : 0);
    const bool reserved = field.readType == telemetry::ScalarType::Null && access == 0;
    return out.u32(group) && out.u32(position) && out.u32((group << 16) | position) &&
           out.u32(field.flags().value()) && out.u32(enums) &&
           out.u8(static_cast<std::uint8_t>(toWireType(field.declaredType))) &&
           out.u8(static_cast<std::uint8_t>(toWireType(field.readType))) &&
           out.u8(static_cast<std::uint8_t>(access)) && out.u8(reserved ? Reserved : 0) &&
           labels(out, refs) && limits(out, field.declaredType);
}

template <class Writer>
inline bool parameterPayload(Writer& out, std::uint32_t commandId, const telemetry::CommandParam& p,
                             std::uint32_t enums, const LabelRefs& refs) noexcept
{
    const auto presence = (p.name != nullptr ? HasName : 0) | (p.unit != nullptr ? HasUnit : 0);
    return out.u32(commandId) && out.u32(static_cast<std::uint32_t>(p.index)) && out.u32(0) &&
           out.u32(enums) && out.u8(static_cast<std::uint8_t>(toWireType(p.type))) &&
           out.u8(static_cast<std::uint8_t>(presence)) && out.u16(0) && labels(out, refs) &&
           limits(out, p.type);
}

template <class Writer>
inline bool commandPayload(Writer& out, std::uint32_t group, std::uint32_t position,
                           const telemetry::Command& command, std::uint32_t parameters,
                           StringRef name) noexcept
{
    (void)command;
    return out.u32(group) && out.u32(position) && out.u32((group << 16) | position) &&
           out.u32(parameters) && out.u32(0) && out.string(name.view());
}

// Construction hashes once through the same encoders. These overloads validate
// input and prepare string lengths; READ uses the explicit refs above.
template <class Writer>
inline bool catalogPayload(Writer& out, std::uint32_t index, std::uint32_t count,
                           const char* text) noexcept
{
    StringRef name;
    return stringRef(text, name) ? catalogPayload(out, index, count, name) : out.fail();
}

template <class Writer>
inline bool fieldPayload(Writer& out, std::uint32_t group, std::uint32_t position,
                         const telemetry::Field& field, std::uint32_t enums) noexcept
{
    LabelRefs refs;
    std::uint32_t size;
    return labelRefs(field.name, field.unit, false, refs) && fieldPayloadSize(field, refs, size)
               ? fieldPayload(out, group, position, field, enums, refs)
               : out.fail();
}

template <class Writer>
inline bool parameterPayload(Writer& out, std::uint32_t commandId, const telemetry::CommandParam& p,
                             std::uint32_t enums) noexcept
{
    LabelRefs refs;
    std::uint32_t size;
    return labelRefs(p.name, p.unit, true, refs) && parameterPayloadSize(p, refs, size)
               ? parameterPayload(out, commandId, p, enums, refs)
               : out.fail();
}

template <class Writer>
inline bool commandPayload(Writer& out, std::uint32_t group, std::uint32_t position,
                           const telemetry::Command& command, std::uint32_t parameters) noexcept
{
    StringRef name;
    return stringRef(command.name, name)
               ? commandPayload(out, group, position, command, parameters, name)
               : out.fail();
}
} // namespace telemetry_resource::detail
