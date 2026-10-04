/**
 * @file DeviceResources.hpp
 * @brief Runtime facade; its consumers need no telemetry templates.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#pragma once
#include <resource/FileSystem.hpp>
#include <string_view>

namespace device::resources
{
namespace fileIds
{
inline constexpr resource::FileIndex Descriptor = 0;
inline constexpr resource::FileIndex Values = 1;
} // namespace fileIds

// Borrow only the generic file table at the transport boundary. Its providers
// and telemetry templates stay private to DeviceResources.cpp.
resource::FileSystemView files() noexcept;

std::size_t fileCount() noexcept;
std::string_view path(resource::FileIndex index) noexcept;
resource::FileStat stat(resource::FileIndex index) noexcept;
resource::ReadResult read(resource::FileIndex index, resource::Cursor cursor,
                          resource::Output output) noexcept;
resource::WriteResult write(resource::FileIndex index, resource::Cursor cursor,
                            resource::Input input, bool final = false) noexcept;
// Framing belongs to the application's UART/TCP/etc. transport.
std::size_t handle(resource::Input request, resource::Output response) noexcept;
} // namespace device::resources
