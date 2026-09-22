/**
 * @file BlockStream.hpp
 * @brief Opaque hierarchical cursors and bounded continuation inside one metadata block.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "BinaryWriter.hpp"
#include <telemetry/core/TelemetryId.h>

namespace telemetry_resource::detail
{
enum class BlockKind : std::uint8_t
{
    Prefix,
    Catalog,
    Entry,
    End
};
inline constexpr unsigned kindShift = 62, keyShift = 30;
inline constexpr std::uint32_t offsetMask = (UINT32_C(1) << keyShift) - 1;

// Internal callers supply an offset <= offsetMask. Public cursors are decoded
// and validated, never repacked by truncating an invalid component.
constexpr resource::Cursor pack(BlockKind kind, std::uint32_t key = 0,
                                std::uint32_t offset = 0) noexcept
{
    return (resource::Cursor(kind) << kindShift) | (resource::Cursor(key) << keyShift) | offset;
}

inline constexpr auto endCursor = pack(BlockKind::End);

// Metadata exposes empty catalogs too. Widen the group before advancing past
// 65535; incrementing a packed 0xffffffff ID would wrap to the first field.
constexpr resource::Cursor catalogCursor(std::uint32_t group, std::size_t count) noexcept
{
    return group < count ? pack(BlockKind::Catalog, group) : endCursor;
}

template <class Index>
resource::Cursor nextEntry(const Index& index, std::uint32_t id) noexcept
{
    const auto group = id >> 16;
    const auto position = id & UINT32_C(0xffff);
    const auto* catalog = index.catalog(static_cast<telemetry::GroupId>(group));
    return position + 1 < catalog->count ? pack(BlockKind::Entry, id + 1)
                                         : catalogCursor(group + 1, index.size());
}

class BlockStream
{
public:
    BlockStream(resource::Cursor cursor, resource::Output out) noexcept
        : out_(out.first(std::min<std::size_t>(out.size(), UINT32_MAX))), original_(cursor),
          kind_(static_cast<BlockKind>(cursor >> kindShift)),
          key_(static_cast<std::uint32_t>(cursor >> keyShift)),
          offset_(static_cast<std::uint32_t>(cursor & offsetMask))
    {
        if ((kind_ == BlockKind::Prefix && key_ != 0) ||
            (kind_ == BlockKind::End && (key_ != 0 || offset_ != 0)))
        {
            fail(resource::Status::InvalidCursor);
        }
    }

    BlockKind kind() const noexcept
    {
        return kind_;
    }

    std::uint32_t key() const noexcept
    {
        return key_;
    }

    std::uint32_t offset() const noexcept
    {
        return offset_;
    }

    bool active() const noexcept
    {
        return status_ == resource::Status::Ok && !stopped_ && kind_ != BlockKind::End;
    }

    // Finish validates offset == block size, including a caller starting right
    // at its end. An offset past the last record is never accepted as EOF.
    bool finish(resource::Cursor next) noexcept
    {
        if (!active())
        {
            return false;
        }
        if (offset_ != position_)
        {
            return fail(resource::Status::InvalidCursor);
        }
        kind_ = static_cast<BlockKind>(next >> kindShift);
        key_ = static_cast<std::uint32_t>(next >> keyShift);
        offset_ = position_ = 0;
        return true;
    }

    template <class Emit>
    bool rawRecord(std::uint32_t width, Emit emit) noexcept
    {
        if (!active())
        {
            return false;
        }
        if (width > offsetMask - position_)
        {
            return fail(resource::Status::InvalidData);
        }
        if (offset_ >= position_ + width)
        {
            position_ += width;
            return true;
        }
        if (used_ == out_.size())
        {
            return pause();
        }
        const auto skip = offset_ - position_;
        BinaryWriter out{out_.subspan(used_), skip};
        const bool done = emit(out);
        if (!out.ok() || (!done && !out.full()) ||
            (done && (out.skip() != 0 || out.used() != width - skip)) || out.used() > width - skip)
        {
            return fail(resource::Status::InvalidData);
        }
        used_ += out.used();
        offset_ += static_cast<std::uint32_t>(out.used());
        if (!done)
        {
            return pause();
        }
        position_ += width;
        return true;
    }

    template <class Emit>
    bool record(std::uint8_t type, Emit emit) noexcept
    {
        if (!active())
        {
            return false;
        }
        BinaryWriter writer;
        if (!emit(writer) || !writer.ok() || writer.count() > offsetMask - recordHeaderSize)
        {
            return fail(resource::Status::InvalidData);
        }
        const auto size = writer.count();
        const auto width = recordHeaderSize + size;
        if (width > offsetMask - position_)
        {
            return fail(resource::Status::InvalidData);
        }
        if (offset_ >= position_ + width)
        {
            position_ += width;
            return true;
        }
        if (used_ == out_.size())
        {
            return pause();
        }
        const auto skip = offset_ - position_;
        // Reuse the measure writer's lifetime/storage. Two simultaneously live
        // writers would add stack at every level of the metadata visitor chain.
        writer = BinaryWriter{out_.subspan(used_), skip};
        const bool done =
            writer.u8(type) && writer.u8(1) && writer.u16(0) && writer.u32(size) && emit(writer);
        if (!writer.ok() || (!done && !writer.full()) ||
            (done && (writer.skip() != 0 || writer.used() != width - skip)) ||
            writer.used() > width - skip)
        {
            return fail(resource::Status::InvalidData);
        }
        used_ += writer.used();
        offset_ += static_cast<std::uint32_t>(writer.used());
        if (!done)
        {
            return pause();
        }
        position_ += width;
        return true;
    }

    bool fixedRecord(resource::Input bytes) noexcept
    {
        if (bytes.size() > offsetMask)
        {
            return fail(resource::Status::InvalidData);
        }
        return rawRecord(static_cast<std::uint32_t>(bytes.size()),
                         [&](BinaryWriter& out) noexcept
                         {
                             return out.bytes(bytes);
                         });
    }

    template <class Emit>
    bool atomic(std::uint32_t width, Emit emit) noexcept
    {
        static_assert(noexcept(emit(resource::Output{})));
        if (!active())
        {
            return false;
        }
        if (offset_ != 0)
        {
            return fail(resource::Status::InvalidCursor);
        }
        if (width == 0 || width > offsetMask)
        {
            return fail(resource::Status::InvalidData);
        }
        if (width > out_.size() - used_)
        {
            return pause();
        }
        emit(out_.subspan(used_, width));
        used_ += width;
        offset_ = position_ = width;
        return true;
    }

    bool fail(resource::Status status) noexcept
    {
        status_ = status;
        return false;
    }

    resource::ReadResult result() const noexcept
    {
        if (status_ != resource::Status::Ok)
        {
            return {status_, original_};
        }
        return {resource::Status::Ok, pack(kind_, key_, offset_), static_cast<std::uint32_t>(used_),
                kind_ == BlockKind::End};
    }

private:
    bool pause() noexcept
    {
        stopped_ = true;
        // An exact-end cursor can advance even with an empty destination.
        if (used_ == 0 && pack(kind_, key_, offset_) == original_)
        {
            status_ = resource::Status::BufferTooSmall;
        }
        return false;
    }

    resource::Output out_;
    resource::Cursor original_;
    std::size_t used_ = 0;
    BlockKind kind_;
    std::uint32_t key_, offset_;
    std::uint32_t position_ = 0; // Bytes traversed only inside the current block.
    resource::Status status_ = resource::Status::Ok;
    bool stopped_ = false;
};
} // namespace telemetry_resource::detail
