/**
 * @file SchemaFile.hpp
 * @brief Borrowed immutable field schema exposed as a bounded resource stream.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include <resource/Types.hpp>
#include <telemetry/abi/TelemetryAbi.h>

namespace telemetry_resource
{
// Construction counts metadata once. It copies the index's pointer/count,
// allowing SchemaFile{fields.index()}; the underlying catalogs must outlive it.
// The ABI tag protects the compiled adapter boundary just like telemetry JSON.
class SchemaFile
{
public:
    explicit SchemaFile(const telemetry::CatalogIndex& index,
                        telemetry::detail::CurrentAbiTag = {}) noexcept;

    resource::FileSize size() const noexcept
    {
        return size_;
    }

    resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept;

private:
    const telemetry::Catalog* catalogs_;
    std::size_t count_;
    // Cached metadata only; no serialized document or runtime values are kept.
    std::uint32_t hash_ = 0;
    std::uint32_t size_ = 0;
    std::uint32_t records_ = 0;
};
} // namespace telemetry_resource
