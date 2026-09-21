/**
 * @file FileSystem.hpp
 * @brief Flat constant descriptor table with O(1) runtime indexing.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE.
 */
#pragma once
#include "File.hpp"
#include <array>
#include <limits>

namespace resource
{
template <std::size_t N>
class FileSystem;

// Small borrowed runtime facade. The table, paths and providers must remain
// alive at stable addresses. No directory parsing, allocation or telemetry.
class FileSystemView
{
public:
    constexpr FileSystemView() noexcept = default;

    constexpr std::size_t fileCount() const noexcept
    {
        return count_;
    }

    constexpr std::string_view path(FileIndex index) const noexcept
    {
        return index < count_ ? files_[index].path : std::string_view{};
    }

    [[nodiscard]] FileStat stat(FileIndex index) const noexcept
    {
        if (index >= count_)
        {
            return {Status::InvalidFile};
        }
        const auto& file = files_[index];
        // Capability has one source of truth: a present operation callback.
        const auto flags = (file.ops->read ? FileFlag::Readable : FileFlag::None) |
                           (file.ops->write ? FileFlag::Writable : FileFlag::None);
        return {Status::Ok, file.ops->size(file.object), flags};
    }

    [[nodiscard]] ReadResult read(FileIndex index, Cursor cursor, Output out) const noexcept
    {
        if (index >= count_)
        {
            return {Status::InvalidFile, cursor};
        }
        const auto& file = files_[index];
        if (!file.ops->read)
        {
            return {Status::NotReadable, cursor};
        }
        return file.ops->read(file.object, cursor, out);
    }

    [[nodiscard]] WriteResult write(FileIndex index, Cursor cursor, Input in,
                                    bool final = false) const noexcept
    {
        if (index >= count_)
        {
            return {Status::InvalidFile, cursor};
        }
        const auto& file = files_[index];
        if (!file.ops->write)
        {
            return {Status::NotWritable, cursor};
        }
        return file.ops->write(file.object, cursor, in, final);
    }

private:
    template <std::size_t>
    friend class FileSystem;

    constexpr FileSystemView(const FileEntry* files, std::size_t count) noexcept
        : files_(files), count_(count)
    {
    }

    const FileEntry* files_ = nullptr;
    std::size_t count_ = 0;
};

template <std::size_t N>
class FileSystem
{
    static_assert(N <= std::numeric_limits<FileIndex>::max(), "Too many resource files");

public:
    constexpr explicit FileSystem(std::array<FileEntry, N> entries) noexcept : files_(entries)
    {
        // Definition validation happens once. Runtime lookup never scans paths.
        for (std::size_t i = 0; i < N; ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                if (files_[i].path == files_[j].path)
                {
                    detail::invalidDefinition();
                }
            }
        }
    }

    constexpr FileSystemView view() const& noexcept
    {
        return {files_.data(), N};
    }

    FileSystemView view() const&& = delete;

    static constexpr std::size_t fileCount() noexcept
    {
        return N;
    }

    constexpr std::string_view path(FileIndex index) const noexcept
    {
        return view().path(index);
    }

    [[nodiscard]] FileStat stat(FileIndex index) const noexcept
    {
        return view().stat(index);
    }

    [[nodiscard]] ReadResult read(FileIndex index, Cursor cursor, Output output) const noexcept
    {
        return view().read(index, cursor, output);
    }

    [[nodiscard]] WriteResult write(FileIndex index, Cursor cursor, Input input,
                                    bool final = false) const noexcept
    {
        return view().write(index, cursor, input, final);
    }

private:
    const std::array<FileEntry, N> files_;
};

template <class... Entry>
    requires(std::same_as<Entry, FileEntry> && ...)
constexpr auto filesystem(Entry... entries) noexcept
{
    return FileSystem<sizeof...(Entry)>{std::array<FileEntry, sizeof...(Entry)>{entries...}};
}
} // namespace resource
