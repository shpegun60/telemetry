/**
 * @file TelemetryCommandJson.h
 * @brief Optional JSON schemas for commands inferred from C++ signatures.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_JSON_H
#define TELEMETRY_COMMAND_JSON_H

#include "TelemetryJson.h"
#include "../command/TelemetryCommandCatalogIndex.h"
#include "../command/TelemetryCommandIndex.h"

namespace telemetry {
namespace detail {
// Local and grouped indexes have distinct wire envelopes and fingerprints.
// Their compiled boundaries share the exact ABI tag used by field schemas.
std::uint32_t commandSchemaCrcAbi(const CommandIndex&, CurrentAbiTag) noexcept;
std::size_t writeCommandSchemaAbi(const CommandIndex&, char*, std::size_t, JsonOptions, CurrentAbiTag) noexcept;
std::uint32_t commandSchemaCrcAbi(const CommandCatalogIndex&, CurrentAbiTag) noexcept;
std::size_t writeCommandSchemaAbi(const CommandCatalogIndex&, char*, std::size_t,
                                  JsonOptions, CurrentAbiTag) noexcept;
} // namespace detail

// Same bounded-buffer and UTF-8 contract as field schemas. Metadata-less
// parameters omit n/u; every parameter has i/t/min/max/default and optional
// enum. Schema export never invokes the command or reads its owner.
// Defaults are UI hints, never implicit execution arguments. Commands have a
// separate dense ID space, starting at zero. CRC covers logical metadata,
// independently of U64/S64 number/string mode and callback addresses.
template <class Abi = detail::CurrentAbiTag>
inline std::uint32_t schemaCrc(const CommandIndex& index) noexcept
{
    return detail::commandSchemaCrcAbi(index, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeSchema(const CommandIndex& index, char* buffer, std::size_t size,
                               JsonOptions options = {}) noexcept
{
    return detail::writeCommandSchemaAbi(index, buffer, size, options, Abi{});
}

// Grouped commands use packed group/index IDs and slash-path catalog names.
// A slash is ordinary label text here; serialization does not split or
// normalize it, and reordering groups changes their positional identities.
// Command itself stays unchanged; the catalog owns hierarchy once per group.
template <class Abi = detail::CurrentAbiTag>
inline std::uint32_t schemaCrc(const CommandCatalogIndex& index) noexcept
{
    return detail::commandSchemaCrcAbi(index, Abi{});
}

template <class Abi = detail::CurrentAbiTag>
inline std::size_t writeSchema(const CommandCatalogIndex& index, char* buffer,
                               std::size_t size, JsonOptions options = {}) noexcept
{
    return detail::writeCommandSchemaAbi(index, buffer, size, options, Abi{});
}
} // namespace telemetry
#endif
