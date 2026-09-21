/**
 * @file CommandsFile.hpp
 * @brief Stream grouped command metadata without retaining parameter visitors.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include <resource/Types.hpp>
#include <telemetry/abi/TelemetryAbi.h>

namespace telemetry_resource
{
// Copy the small index view, borrow the immutable command catalogs. Parameter
// descriptions are generated synchronously when counting or emitting a record.
class CommandsFile
{
public:
    explicit CommandsFile(const telemetry::CommandCatalogIndex& index,
                          telemetry::detail::CurrentAbiTag = {}) noexcept;

    resource::FileSize size() const noexcept
    {
        return size_;
    }

    resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept;

private:
    const telemetry::CommandCatalog* catalogs_;
    std::size_t count_;
    std::uint32_t hash_ = 0;
    std::uint32_t size_ = 0;
    std::uint32_t records_ = 0;
};
} // namespace telemetry_resource
