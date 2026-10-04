/**
 * @file FileView.hpp
 * @brief Borrowed file facade and forward iterator over constant descriptors.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE.
 */
#pragma once
#include "File.hpp"
#include <iterator>

namespace resource
{
class FileSystemView;
class FileIterator;

// Borrows one descriptor directly, never the FileSystemView that returned it.
// The descriptor table, its path and provider must outlive this facade.
class FileView
{
public:
    constexpr FileView() noexcept = default;

    constexpr FileIndex index() const noexcept
    {
        return index_;
    }

    constexpr bool valid() const noexcept
    {
        return file_ != nullptr;
    }

    constexpr explicit operator bool() const noexcept
    {
        return valid();
    }

    constexpr std::string_view path() const noexcept
    {
        return file_ ? file_->path : std::string_view{};
    }

    // Inspect declared capabilities without calling the provider.
    constexpr bool readable() const noexcept
    {
        return file_ && file_->ops->read != nullptr;
    }

    constexpr bool writable() const noexcept
    {
        return file_ && file_->ops->write != nullptr;
    }

    [[nodiscard]] FileStat stat() const noexcept
    {
        if (!file_)
        {
            return {Status::InvalidFile};
        }
        const auto flags = (file_->ops->read ? FileFlag::Readable : FileFlag::None) |
                           (file_->ops->write ? FileFlag::Writable : FileFlag::None);
        return {Status::Ok, file_->ops->size(file_->object), flags};
    }

    [[nodiscard]] ReadResult read(Cursor cursor, Output out) const noexcept
    {
        if (!file_)
        {
            return {Status::InvalidFile, cursor};
        }
        if (!file_->ops->read)
        {
            return {Status::NotReadable, cursor};
        }
        return file_->ops->read(file_->object, cursor, out);
    }

    [[nodiscard]] WriteResult write(Cursor cursor, Input in, bool final = false) const noexcept
    {
        if (!file_)
        {
            return {Status::InvalidFile, cursor};
        }
        if (!file_->ops->write)
        {
            return {Status::NotWritable, cursor};
        }
        return file_->ops->write(file_->object, cursor, in, final);
    }

private:
    friend class FileSystemView;
    friend class FileIterator;

    constexpr FileView(const FileEntry* file, FileIndex index) noexcept
        : file_(file), index_(index)
    {
    }

    const FileEntry* file_ = nullptr;
    FileIndex index_ = 0;
};

// Enumeration reads descriptors only. stat/read/write are explicit operations
// on the returned FileView. Incrementing or dereferencing end is outside the
// iterator contract, as with other forward iterators.
class FileIterator
{
public:
    using value_type = FileView;
    using reference = FileView;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    constexpr FileIterator() noexcept = default;

    constexpr FileView operator*() const noexcept
    {
        return {std::addressof(files_[index_]), index_};
    }

    constexpr FileIterator& operator++() noexcept
    {
        ++index_;
        return *this;
    }

    constexpr FileIterator operator++(int) noexcept
    {
        auto previous = *this;
        ++*this;
        return previous;
    }

    friend constexpr bool operator==(const FileIterator&, const FileIterator&) noexcept = default;

private:
    friend class FileSystemView;

    constexpr FileIterator(const FileEntry* files, FileIndex index) noexcept
        : files_(files), index_(index)
    {
    }

    const FileEntry* files_ = nullptr;
    FileIndex index_ = 0;
};
} // namespace resource
