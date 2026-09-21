/**
 * @file DeviceResources.hpp
 * @brief Small application facade; consumers need no telemetry headers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include <resource/Types.hpp>
#include <string_view>

namespace device::resources
{
namespace files
{
inline constexpr resource::FileIndex Schema = 0;
inline constexpr resource::FileIndex Commands = 1;
inline constexpr resource::FileIndex Values = 2;
} // namespace files

std::size_t fileCount() noexcept;
std::string_view path(resource::FileIndex index) noexcept;
resource::FileStat stat(resource::FileIndex index) noexcept;
resource::ReadResult read(resource::FileIndex index, resource::Cursor cursor,
                          resource::Output output) noexcept;
resource::WriteResult write(resource::FileIndex index, resource::Cursor cursor,
                            resource::Input input, bool final = false) noexcept;
// Returns response bytes, or zero when no complete response fits. The first
// response byte is the protocol status. Framing belongs to UART/TCP/etc.
std::size_t handle(resource::Input request, resource::Output response) noexcept;
} // namespace device::resources
