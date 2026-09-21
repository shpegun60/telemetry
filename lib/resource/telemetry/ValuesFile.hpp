/**
 * @file ValuesFile.hpp
 * @brief Fixed-width hex readings; one getter invocation per whole value token.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include <resource/Types.hpp>
#include <telemetry/abi/TelemetryAbi.h>

namespace telemetry_resource
{
// Metadata is borrowed and immutable. Readings remain live: repeated reads
// can return newer values; this adapter does not acquire a cross-field snapshot.
class ValuesFile
{
public:
    explicit ValuesFile(const telemetry::CatalogIndex& index,
                        telemetry::detail::CurrentAbiTag = {}) noexcept;

    resource::FileSize size() const noexcept
    {
        return size_;
    }

    resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept;

private:
    const telemetry::Catalog* catalogs_;
    std::size_t count_;
    // Widths come from declared types, so these counts never require a getter.
    std::uint32_t size_ = 0;
    std::uint32_t records_ = 0;
};
} // namespace telemetry_resource
