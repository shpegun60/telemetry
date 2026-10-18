/*
 * @file Layout.cpp
 * @brief ARM row/index ABI footprint and size-independent positional codegen.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <telemetry/Telemetry.hpp>
#include <integration/telemetry/v3/detail/Values.hpp>
#include <telemetry/descriptor/detail/Segments.hpp>
#include <cstdint>

extern "C" {
extern const std::uint32_t scalability_layout[]{sizeof(telemetry::FieldEntry),
                                                sizeof(telemetry::CommandEntry),
                                                sizeof(telemetry::ServiceEntry),
                                                sizeof(telemetry::FieldCatalog),
                                                sizeof(telemetry::ModelView),
                                                sizeof(resource::telemetry::v3::detail::ValueToken),
                                                sizeof(::telemetry::descriptor::detail::Segment)};

const telemetry::FieldEntry* scalability_find(const telemetry::FieldIndex& index,
                                              telemetry::PackedId id) noexcept
{
	return index.find(id);
}

telemetry::EncodedReadResult scalability_read(const telemetry::FieldIndex& index,
                                              telemetry::PackedId id, std::span<std::byte> output,
                                              telemetry::Workspace& workspace) noexcept
{
	return index.readEncoded(id, output, workspace);
}
}
