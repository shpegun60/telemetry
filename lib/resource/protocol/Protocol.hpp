/**
 * @file Protocol.hpp
 * @brief One bounded little-endian resource request/response, without framing.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include <resource/FileSystem.hpp>

namespace resource_protocol
{
enum class Op : std::uint8_t
{
    List = 1,
    Stat = 2,
    Read = 3,
    Write = 4
};

struct Reply
{
    resource::Status status;
    std::size_t written;
};

// Consume exactly one complete packet. Framing, checksums and retransmission
// belong to the caller. No write is invoked unless the complete reply fits.
// Malformed packets produce a one-byte status, if space permits. Well-formed
// operations use their normal reply envelope, including on provider errors.
[[nodiscard]] Reply process(resource::FileSystemView files, resource::Input request,
                            resource::Output response) noexcept;
} // namespace resource_protocol
