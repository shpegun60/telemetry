// Reject C++ heap use during resource assembly and transfer (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <telemetry/Telemetry.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <new>

bool allocationsAllowed = true;

void* operator new(std::size_t size)
{
    if (!allocationsAllowed)
    {
        std::abort();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size))
    {
        return memory;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

float voltage() noexcept
{
    return 230.0f;
}

telemetry::CommandResult configure(float) noexcept
{
    return telemetry::CommandResult::Executed;
}

constexpr telemetry::FieldTable fields{
    telemetry::field<&voltage>("Voltage", "V", telemetry::limits(230.0f, 0.1f, 300.0f))};
constexpr telemetry::FieldCatalogTable catalogs{telemetry::group("meter", fields)};
constexpr telemetry::CommandTable commands{telemetry::command<&configure>(
    "Configure", telemetry::arg<0>("Voltage", "V", 230.0f, 0.1f, 300.0f))};
constexpr telemetry::CommandCatalogTable commandCatalogs{telemetry::group("meter", commands)};

int main()
{
    allocationsAllowed = false;
    telemetry_resource::SchemaFile schema{catalogs.index()};
    telemetry_resource::CommandsFile commandSchema{commandCatalogs.index()};
    telemetry_resource::ValuesFile values{catalogs.index()};
    const auto files = resource::filesystem(resource::file("/schema", schema),
                                            resource::file("/commands", commandSchema),
                                            resource::file("/values", values));

    std::array<std::byte, 31> output{};
    for (resource::FileIndex index = 0; index < files.fileCount(); ++index)
    {
        resource::Cursor cursor = 0;
        for (std::size_t count = 0;; ++count)
        {
            if (count > files.stat(index).size)
            {
                std::abort();
            }
            const auto result = files.read(index, cursor, output);
            if (result.status != resource::Status::Ok)
            {
                std::abort();
            }
            if (result.eof)
            {
                break;
            }
            cursor = result.next;
        }
    }
    const std::array<std::byte, 9> list{std::byte{1}};
    if (resource_protocol::process(files.view(), list, output).status != resource::Status::Ok)
    {
        std::abort();
    }
    allocationsAllowed = true;
    std::puts("Resource assembly and transfers: no C++ heap allocations");
}
