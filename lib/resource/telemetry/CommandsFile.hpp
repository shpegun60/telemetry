/**
 * @file CommandsFile.hpp
 * @brief Descriptive binary command/parameter records, without executing commands.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include <resource/Types.hpp>
#include <telemetry/abi/TelemetryAbi.h>

namespace telemetry_resource
{
// Catalogs and all borrowed metadata must outlive the file and remain immutable.
class CommandsFile
{
public:
    explicit CommandsFile(const telemetry::CommandCatalogIndex& index,
                          telemetry::detail::CurrentAbiTag = {}) noexcept;

    resource::FileSize size() const noexcept
    {
        return size_;
    }

    std::uint32_t fingerprint() const noexcept
    {
        return hash_;
    }

    resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept;

private:
    const telemetry::CommandCatalog* catalogs_;
    std::size_t count_;
    std::uint32_t hash_ = 0;
    std::uint32_t size_ = 0;
    std::uint32_t records_ = 0;
    std::uint32_t commands_ = 0;
    std::uint32_t parameters_ = 0;
    std::uint32_t enums_ = 0;
};
} // namespace telemetry_resource
