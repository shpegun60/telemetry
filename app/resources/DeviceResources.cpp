/**
 * @file DeviceResources.cpp
 * @brief The sole assembly point for telemetry-backed demo resources.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#include "DeviceResources.hpp"
#include "../demo/DemoCatalog.h"
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <resource/telemetry/TelemetryFiles.hpp>

namespace
{
telemetry_resource::SchemaFile schema{demo::fields.index()};
telemetry_resource::CommandsFile commands{demo::commands.index()};
telemetry_resource::ValuesFile values{demo::fields.index()};
// Providers hold small runtime views; the descriptor table needs no startup
// construction and can live in read-only storage. Initialization finishes
// before main; application transports must not read it from other initializers.
constinit const auto fs = resource::filesystem(resource::file("/telemetry/schema.json", schema),
                                               resource::file("/telemetry/commands.json", commands),
                                               resource::file("/telemetry/values.json", values));
static_assert(decltype(fs)::fileCount() == 3);
} // namespace

namespace device::resources
{
std::size_t fileCount() noexcept
{
    return fs.fileCount();
}

std::string_view path(resource::FileIndex index) noexcept
{
    return fs.path(index);
}

resource::FileStat stat(resource::FileIndex index) noexcept
{
    return fs.stat(index);
}

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
