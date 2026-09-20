/**
 * @file TelemetryCommandJson.cpp
 * @brief Stream command parameters into JSON without a runtime descriptor array.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "TelemetryCommandJson.h"
#include "../detail/TelemetryJsonValue.h"
#include <cstring>

namespace telemetry {
namespace {

// Fingerprints describe parameter contracts, labels and positional identity.
// Callback/owner addresses and the selected 64-bit JSON representation are
// deliberately absent, so relocation does not change a logical schema.
std::uint32_t byte(std::uint32_t hash, std::uint8_t value) noexcept { return (hash ^ value) * 16777619u; }
std::uint32_t word(std::uint32_t hash, std::uint64_t value) noexcept
{
    for (unsigned shift = 0; shift < 64; shift += 8) hash = byte(hash, static_cast<std::uint8_t>(value >> shift));
    return hash;
}
std::uint32_t string(std::uint32_t hash, const char* value) noexcept
{
    // Presence marker distinguishes a missing label from an explicit empty one.
    hash = byte(hash, value != nullptr);
    if (value != nullptr) { for (; *value != '\0'; ++value) hash = byte(hash, static_cast<std::uint8_t>(*value)); }
    return byte(hash, 0);
}
std::uint32_t scalar(std::uint32_t hash, const Scalar& value) noexcept
{
    hash = byte(hash, static_cast<std::uint8_t>(value.type()));
    if (value.type() == ScalarType::Null) return hash;
    if (value.type() == ScalarType::F32) {
        const auto number = value.get<float>();
        std::uint32_t bits;
        std::memcpy(&bits, &number, sizeof(bits));
        return word(hash, bits);
    }
    if (value.type() == ScalarType::F64) {
        const auto number = value.get<double>();
        std::uint64_t bits;
        std::memcpy(&bits, &number, sizeof(bits));
        return word(hash, bits);
    }
    const auto number = convertScalar<std::int64_t>(value);
    return word(hash, number ? static_cast<std::uint64_t>(*number) : value.get<std::uint64_t>());
}
bool hashEnum(void* context, const Scalar& code, std::string_view name) noexcept
{
    auto& hash = *static_cast<std::uint32_t*>(context);
    hash = scalar(byte(hash, 'V'), code);
    hash = word(hash, name.size());
    for (const auto ch : name) hash = byte(hash, static_cast<std::uint8_t>(ch));
    return true;
}
bool hashParameter(void* context, const CommandParam& parameter) noexcept
{
    auto& hash = *static_cast<std::uint32_t*>(context);
    hash = word(byte(hash, 'P'), parameter.index);
    hash = string(string(hash, parameter.name), parameter.unit);
    hash = byte(hash, static_cast<std::uint8_t>(static_cast<ScalarType>(parameter.type)));
    hash = scalar(scalar(scalar(hash, parameter.type.minimum()), parameter.type.maximum()), parameter.type.defaultValue());
    if (parameter.type.hasEnum()) {
        hash = byte(hash, 'D');
        if (!parameter.type.describeEnum(&hash, &hashEnum)) return false;
        hash = byte(hash, 'd');
    }
    return true;
}

bool hashCommand(std::uint32_t& hash, const Command& command, CommandId id) noexcept
{
    if (command.name == nullptr) return false;
    hash = word(byte(hash, 'C'), id);
    hash = string(hash, command.name);
    if (command.describe != nullptr && !command.describeParameters(&hash, &hashParameter)) return false;
    hash = byte(hash, 'E');
    return true;
}

bool appendParameter(void* context, const CommandParam& parameter) noexcept
{
    // Generated descriptions visit dense signature positions in order. Their
    // CommandParam objects are temporary: consume each synchronously and keep
    // no pointer to it after this sink returns.
    auto& out = *static_cast<detail::JsonWriter*>(context);
    if (!out.append("%s{\"i\":%lu", parameter.index == 0 ? "" : ",",
                    static_cast<unsigned long>(parameter.index))) return false;
    if (parameter.name != nullptr && (!out.append(",\"n\":") || !out.appendRequiredString(parameter.name))) return false;
    if (parameter.unit != nullptr && (!out.append(",\"u\":") || !out.appendRequiredString(parameter.unit))) return false;
    const bool hasEnum = parameter.type.hasEnum();
    if (!out.append(",\"t\":\"%s\",\"min\":", detail::scalarTypeName(parameter.type))
        || !detail::appendBound(out, parameter.type.minimum(), true, hasEnum)
        || !out.append(",\"max\":") || !detail::appendBound(out, parameter.type.maximum(), false, hasEnum)
        || !out.append(",\"default\":") || !detail::appendMetadata(out, parameter.type.defaultValue())) return false;
    if (hasEnum) {
        detail::EnumJsonContext state{out};
        if (!out.append(",\"enum\":{") || !parameter.type.describeEnum(&state, &detail::appendEnumEntry)
            || !out.append("}")) return false;
    }
    return out.append("}");
}
} // namespace

namespace detail {
std::uint32_t commandSchemaCrcAbi(const CommandIndex& index, CurrentAbiTag) noexcept
{
    // Separate root markers keep local and grouped command namespaces apart.
    std::uint32_t hash = byte(2166136261u, 'M');
    for (std::size_t i = 0; i < index.size(); ++i) {
        const auto& command = index.data()[i];
        if (!hashCommand(hash, command, static_cast<CommandId>(i))) return 0;
    }
    return hash;
}

std::size_t writeCommandSchemaAbi(const CommandIndex& index, char* buffer, std::size_t size,
                                 JsonOptions options, CurrentAbiTag tag) noexcept
{
    JsonWriter out{buffer, size, options.int64 == JsonInt64Mode::String};
    if (!out.ok()) return 0;
    if (!out.append("{\"schema\":\"%08lx\",\"commands\":[",
                    static_cast<unsigned long>(commandSchemaCrcAbi(index, tag)))) return 0;
    for (std::size_t i = 0; i < index.size(); ++i) {
        const auto& command = index.data()[i];
        if (!out.append("%s{\"id\":%" PRIu32 ",\"n\":", i == 0 ? "" : ",", static_cast<CommandId>(i))
            || !out.appendRequiredString(command.name) || !out.append(",\"params\":[")
            || (command.describe != nullptr && !command.describeParameters(&out, &appendParameter)) || !out.append("]}")) return 0;
    }
    (void) out.append("]}");
    return out.length();
}

std::uint32_t commandSchemaCrcAbi(const CommandCatalogIndex& index,
                                  CurrentAbiTag) noexcept
{
    std::uint32_t hash = byte(2166136261u, 'G');
    for (std::size_t group = 0; group < index.size(); ++group) {
        const auto& catalog = index.data()[group];
        if (catalog.name == nullptr) return 0;
        hash = word(byte(hash, 'g'), group);
        hash = string(hash, catalog.name);
        for (std::size_t i = 0; i < catalog.count; ++i) {
            if (!hashCommand(hash, catalog.commands[i], makeId(static_cast<GroupId>(group), static_cast<CommandOffset>(i)))) return 0;
        }
        hash = byte(hash, 'e');
    }
    return hash;
}

std::size_t writeCommandSchemaAbi(const CommandCatalogIndex& index,
                                  char* buffer, std::size_t size,
                                  JsonOptions options, CurrentAbiTag tag) noexcept
{
    // Both loops use constructor-capped bounds; packing the positions cannot
    // narrow a valid group/entry. No command handler is invoked during export.
    JsonWriter out{buffer, size, options.int64 == JsonInt64Mode::String};
    if (!out.ok()) return 0;
    if (!out.append("{\"schema\":\"%08lx\",\"commandCatalogs\":[",
                    static_cast<unsigned long>(commandSchemaCrcAbi(index, tag)))) return 0;
    for (std::size_t group = 0; group < index.size(); ++group) {
        const auto& catalog = index.data()[group];
        if (!out.append("%s{\"id\":%u,\"name\":", group == 0 ? "" : ",",
                        static_cast<unsigned>(group))
            || !out.appendRequiredString(catalog.name)
            || !out.append(",\"commands\":[")) return 0;
        for (std::size_t i = 0; i < catalog.count; ++i) {
            const auto& command = catalog.commands[i];
            if (!out.append("%s{\"i\":%lu,\"id\":%" PRIu32 ",\"n\":",
                            i == 0 ? "" : ",", static_cast<unsigned long>(i), makeId(static_cast<GroupId>(group), static_cast<CommandOffset>(i)))
                || !out.appendRequiredString(command.name)
                || !out.append(",\"params\":[")
                || (command.describe != nullptr && !command.describeParameters(&out, &appendParameter))
                || !out.append("]}")) return 0;
        }
        if (!out.append("]}")) return 0;
    }
    (void) out.append("]}");
    return out.length();
}
} // namespace detail
} // namespace telemetry
