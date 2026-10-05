/**
 * @file Protocol.hpp
 * @brief One bounded little-endian resource request/response, without framing.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 *
 * Adapt complete transport packets to the flat file API while leaving
 * connection state and framing with the application. The caller owns both
 * byte spans and sends only the written prefix of the returned reply.
 */

#ifndef TELEMETRY_LIB_RESOURCE_PROTOCOL_PROTOCOL_HPP
#define TELEMETRY_LIB_RESOURCE_PROTOCOL_PROTOCOL_HPP
#pragma once

#include <resource/FileSystem.hpp>
#include "Wire.hpp"

namespace resource::protocol {
struct Reply {
	resource::Status status;
	std::size_t written;
};

// Consume exactly one complete packet. Framing, checksums and retransmission
// belong to the caller. No write is invoked unless the complete reply fits.
// Malformed packets produce a one-byte status, if space permits. Well-formed
// operations use their normal reply envelope, including on provider errors.
[[nodiscard]] Reply process(resource::FileSystemView files, resource::Input request,
                            resource::Output response) noexcept;
} // namespace resource::protocol

#endif // TELEMETRY_LIB_RESOURCE_PROTOCOL_PROTOCOL_HPP
