/**
 * @file Types.hpp
 * @brief Transport-independent resource results and non-owning byte ranges.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace resource
{
using FileIndex = std::uint32_t;
using FileSize = std::uint32_t;
using Cursor = std::uint64_t;
using Output = std::span<std::byte>;
using Input = std::span<const std::byte>;

enum class Status : std::uint8_t
{
    Ok = 0,
    InvalidFile = 1,
    NotReadable = 2,
    NotWritable = 3,
    InvalidCursor = 4,
    CursorExpired = 5,
    BufferTooSmall = 6,
    InvalidData = 7,
    InternalError = 8
};
enum class FileFlag : std::uint8_t
{
    None = 0,
    Readable = 1,
    Writable = 2
};
using FileFlags = FileFlag;

constexpr FileFlags operator|(FileFlags a, FileFlags b) noexcept
{
    return static_cast<FileFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool has(FileFlags value, FileFlag bit) noexcept
{
    return (static_cast<unsigned>(value) & static_cast<unsigned>(bit)) ==
           static_cast<unsigned>(bit);
}

// Cursor zero starts a transfer. Only the provider interprets other cursors.
// For Ok, written/consumed never exceeds the supplied span. On core errors
// no provider is invoked, the cursor is unchanged and the count is zero.
struct ReadResult
{
    Status status = Status::Ok;
    Cursor next = 0;
    std::uint32_t written = 0;
    bool eof = false;
};

struct WriteResult
{
    Status status = Status::Ok;
    Cursor next = 0;
    std::uint32_t consumed = 0;
    bool complete = false;
};

struct FileStat
{
    Status status = Status::Ok;
    FileSize size = 0;
    FileFlags flags = FileFlag::None;
};
} // namespace resource
