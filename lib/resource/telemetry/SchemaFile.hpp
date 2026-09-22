/**
 * @file SchemaFile.hpp
 * @brief Descriptive binary field metadata, measured once without reading values.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include "detail/Fingerprint.hpp"
#include <resource/Types.hpp>
#include <telemetry/abi/TelemetryAbi.h>

namespace telemetry_resource
{
// Copies the index view, borrows immutable descriptors/strings at stable addresses.
// Invalid/unrepresentable metadata gives size()==0 and read()==InvalidData.
class SchemaFile
{
public:
    explicit SchemaFile(const telemetry::CatalogIndex& index,
                        telemetry::detail::CurrentAbiTag = {}) noexcept;

    resource::FileSize size() const noexcept
    {
        return size_;
    }

    std::uint64_t fingerprint() const noexcept
    {
        return hash_.value();
    }

    resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept;

private:
    friend class ValuesFile;
    const telemetry::Catalog* catalogs_;
    std::size_t count_;
    detail::Fingerprint hash_{0};
    std::uint32_t size_ = 0;
    std::uint32_t records_ = 0; // Wire records, excluding the file header.
    std::uint32_t fields_ = 0;
    std::uint32_t enums_ = 0;
    std::uint32_t valuesSize_ = 0;
};
} // namespace telemetry_resource
