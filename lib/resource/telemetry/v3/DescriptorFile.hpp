/*
 * @file DescriptorFile.hpp
 * @brief Read-only resource over immutable packed or streaming descriptors.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef RESOURCE_STRUCTURED_DESCRIPTOR_FILE_HPP
#define RESOURCE_STRUCTURED_DESCRIPTOR_FILE_HPP

#include "../../Types.hpp"
#include <array>
#include <concepts>
#include <cstring>
#include <type_traits>

namespace resource::telemetry::v3 {
namespace detail {

template <class T> inline constexpr bool packedDescriptor = false;
template <std::size_t N>
inline constexpr bool packedDescriptor<std::array<std::byte, N>> = true;

template <class T>
concept DescriptorSource = packedDescriptor<T> || requires(const T& source, Cursor cursor, Output out) {
    { source.size() } noexcept -> std::same_as<FileSize>;
    { source.read(cursor, out) } noexcept -> std::same_as<ReadResult>;
};

} // namespace detail

// Source and its borrowed metadata must remain alive and immutable. Use
// packDescriptor<descriptor>() for a static model; no hash is recomputed here.
template <detail::DescriptorSource Source>
class DescriptorFile {
public:
    template <class S>
        requires (std::same_as<std::remove_cvref_t<S>, Source> &&
                  std::is_lvalue_reference_v<S&&>)
    constexpr explicit DescriptorFile(S&& source) noexcept : source_(&source)
    {
        if constexpr (detail::packedDescriptor<Source>)
            static_assert(std::tuple_size_v<Source> <= UINT32_MAX,
                          "Packed descriptor exceeds resource FileSize");
    }

    [[nodiscard]] constexpr FileSize size() const noexcept
    { return static_cast<FileSize>(source_->size()); }

    [[nodiscard]] ReadResult read(Cursor cursor, Output output) const noexcept
    {
        if constexpr (!detail::packedDescriptor<Source>) {
            return source_->read(cursor, output);
        } else {
            const auto bytes = size();
            if (cursor > bytes) return {Status::InvalidCursor, cursor};
            if (cursor == bytes) return {Status::Ok, cursor, 0, true};
            if (output.empty()) return {Status::BufferTooSmall, cursor};
            const auto remaining = bytes - static_cast<FileSize>(cursor);
            const auto count = static_cast<FileSize>(output.size() < remaining ? output.size() : remaining);
            std::memcpy(output.data(), source_->data() + cursor, count);
            return {Status::Ok, cursor + count, count, cursor + count == bytes};
        }
    }

private:
    const Source* source_;
};

template <class Source>
DescriptorFile(Source&) -> DescriptorFile<std::remove_cv_t<Source>>;

} // namespace resource::telemetry::v3
#endif
