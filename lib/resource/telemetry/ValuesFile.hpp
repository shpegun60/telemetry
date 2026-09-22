/**
 * @file ValuesFile.hpp
 * @brief Fixed-size binary readings with one invocation per complete live value.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include "SchemaFile.hpp"

namespace telemetry_resource
{
// Metadata is immutable and borrowed. Values are live, without a multi-field
// snapshot guarantee; retrying a cursor can yield a newer complete value.
class ValuesFile
{
public:
    explicit ValuesFile(const telemetry::CatalogIndex& index,
                        telemetry::detail::CurrentAbiTag = {}) noexcept;
    // Reuse the schema's measured size/fingerprint; does not borrow the SchemaFile.
    explicit ValuesFile(const SchemaFile& schema) noexcept;

    resource::FileSize size() const noexcept
    {
        return size_;
    }

    resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept;

private:
    const telemetry::Catalog* catalogs_;
    std::size_t count_;
    std::uint32_t size_;
    std::uint32_t fields_;
    std::uint32_t hash_;
};
} // namespace telemetry_resource
