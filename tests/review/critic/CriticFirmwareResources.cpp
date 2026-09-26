// Critic trial (C++20): binary resource files behind the packet protocol for the Cortex-M7 image.
#include "CriticDevice.h"

#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <resource/telemetry/TelemetryFiles.hpp>

namespace {
telemetry_resource::SchemaFile schema{critic::fields.index()};
telemetry_resource::CommandsFile commandFile{critic::commands.index()};
telemetry_resource::ValuesFile values{schema};

constinit const auto files = resource::filesystem(
    resource::file("/telemetry/schema.bin", schema),
    resource::file("/telemetry/commands.bin", commandFile),
    resource::file("/telemetry/values.bin", values));
} // namespace

extern "C" std::size_t criticHandle(const std::uint8_t* request, std::size_t size, std::uint8_t* response,
                                     std::size_t capacity) noexcept
{
    const auto reply = resource_protocol::process(
        files.view(), resource::Input{reinterpret_cast<const std::byte*>(request), size},
        resource::Output{reinterpret_cast<std::byte*>(response), capacity});
    return reply.written;
}
