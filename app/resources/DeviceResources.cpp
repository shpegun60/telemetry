/**
 * @file DeviceResources.cpp
 * @brief Runtime facade for the demo's v3 descriptor and live values.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include "DeviceResources.hpp"
#include "../demo/DemoCatalog.h"
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <resource/telemetry/v3/DescriptorFile.hpp>
#include <resource/telemetry/v3/ValuesFile.hpp>

namespace {
namespace v3 = resource::telemetry::v3;

// Immutable metadata is constructed at compile time. The value provider
// borrows caller-owned scratch and does not require a later init() phase.
inline constexpr v3::Descriptor descriptor{demo::model};
inline constexpr v3::DescriptorFile descriptorFile{descriptor};
alignas(32) std::array<std::byte, 256> scratch;
telemetry::Workspace workspace{scratch};
inline constexpr v3::ValuesFile values{descriptor, workspace};
constinit const auto fs = resource::filesystem(
    resource::file("/telemetry/descriptor.bin", descriptorFile),
    resource::file("/telemetry/values.bin", values));
static_assert(decltype(fs)::fileCount() == 2);
} // namespace

namespace device::resources {
std::size_t fileCount() noexcept { return fs.fileCount(); }
std::string_view path(resource::FileIndex index) noexcept { return fs.path(index); }
resource::FileStat stat(resource::FileIndex index) noexcept { return fs.stat(index); }
resource::ReadResult read(resource::FileIndex index, resource::Cursor cursor,
                          resource::Output output) noexcept
{
    return fs.read(index, cursor, output);
}
resource::WriteResult write(resource::FileIndex index, resource::Cursor cursor,
                            resource::Input input, bool final) noexcept
{
    return fs.write(index, cursor, input, final);
}
std::size_t handle(resource::Input request, resource::Output response) noexcept
{
    return resource_protocol::process(fs.view(), request, response).written;
}
} // namespace device::resources
