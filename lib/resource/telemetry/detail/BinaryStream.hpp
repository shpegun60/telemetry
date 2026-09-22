/**
 * @file BinaryStream.hpp
 * @brief Stateless record continuation; live values are emitted only after complete preflight.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "BinaryWriter.hpp"

namespace telemetry_resource::detail
{
constexpr resource::Cursor pack(std::uint32_t record, std::uint32_t offset = 0) noexcept
{
    return (resource::Cursor{record} << 32) | offset;
}

class BinaryStream
{
public:
    BinaryStream(resource::Cursor cursor, resource::Output out, std::uint32_t records) noexcept
        : out_(out.first(std::min<std::size_t>(out.size(), UINT32_MAX))), original_(cursor),
          target_(static_cast<std::uint32_t>(cursor >> 32)),
          offset_(static_cast<std::uint32_t>(cursor)), records_(records)
    {
        if (target_ > records_ || (target_ == records_ && offset_ != 0))
        {
            fail(resource::Status::InvalidCursor);
        }
    }

    // Used by ValuesFile to reach a positional field without visiting getters.
    std::size_t skipEntries(std::size_t count) noexcept
    {
        const auto skipped =
            std::min<std::size_t>(count, target_ > ordinal_ ? target_ - ordinal_ : 0);
        ordinal_ += static_cast<std::uint32_t>(skipped);
        return skipped;
    }

    bool active() const noexcept
    {
        return status_ == resource::Status::Ok && !stopped_ && target_ < records_;
    }

    // Small already-encoded headers need neither a counting writer nor a
    // second copy of its continuation state. The source is borrowed only here.
    bool fixedRecord(resource::Input bytes) noexcept
    {
        if (!active())
        {
            return false;
        }
        if (ordinal_ < target_)
        {
            ++ordinal_;
            return true;
        }
        if (offset_ > bytes.size())
        {
            return fail(resource::Status::InvalidCursor);
        }
        bytes = bytes.subspan(offset_);
        const auto count = std::min(bytes.size(), out_.size() - used_);
        if (count != 0)
        {
            std::memcpy(out_.data() + used_, bytes.data(), count);
        }
        used_ += count;
        if (count == bytes.size())
        {
            ++ordinal_;
            offset_ = 0;
            return true;
        }
        offset_ += static_cast<std::uint32_t>(count);
        return pause();
    }

    template <class Emit>
    bool rawRecord(std::uint32_t width, Emit emit) noexcept
    {
        if (!active())
        {
            return false;
        }
        if (ordinal_ < target_)
        {
            ++ordinal_;
            return true;
        }
        if (offset_ > width)
        {
            return fail(resource::Status::InvalidCursor);
        }
        BinaryWriter out{out_.subspan(used_), offset_};
        const bool done = emit(out);
        if (!out.ok() || (!done && !out.full()) || (done && out.skip() != 0))
        {
            return fail(resource::Status::InvalidData);
        }
        used_ += out.used();
        if (done)
        {
            ++ordinal_;
            offset_ = 0;
            return true;
        }
        offset_ += static_cast<std::uint32_t>(out.used());
        return pause();
    }

    template <class Emit>
    bool record(std::uint8_t type, Emit emit) noexcept
    {
        if (!active())
        {
            return false;
        }
        if (ordinal_ < target_)
        {
            ++ordinal_;
            return true;
        }
        BinaryWriter writer;
        if (!emit(writer) || !writer.ok() || writer.count() > UINT32_MAX - recordHeaderSize)
        {
            return fail(resource::Status::InvalidData);
        }
        const auto size = writer.count();
        if (offset_ > recordHeaderSize + size)
        {
            return fail(resource::Status::InvalidCursor);
        }
        // Reuse the counting writer's storage. Keeping separate measuring and
        // emitting writers live here would increase each nested visitor frame.
        writer = BinaryWriter{out_.subspan(used_), offset_};
        const bool done =
            writer.u8(type) && writer.u8(1) && writer.u16(0) && writer.u32(size) && emit(writer);
        if (!writer.ok() || (!done && !writer.full()) || (done && writer.skip() != 0))
        {
            return fail(resource::Status::InvalidData);
        }
        used_ += writer.used();
        if (done)
        {
            ++ordinal_;
            offset_ = 0;
            return true;
        }
        offset_ += static_cast<std::uint32_t>(writer.used());
        return pause();
    }

    template <class Emit>
    bool atomic(std::uint32_t width, Emit emit) noexcept
    {
        static_assert(noexcept(emit(resource::Output{})));
        if (!active())
        {
            return false;
        }
        if (ordinal_ < target_)
        {
            ++ordinal_;
            return true;
        }
        if (width == 0)
        {
            return fail(resource::Status::InvalidData);
        }
        if (offset_ != 0)
        {
            return fail(resource::Status::InvalidCursor);
        }
        if (width > out_.size() - used_)
        {
            return pause();
        }
        emit(out_.subspan(used_, width));
        used_ += width;
        ++ordinal_;
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
        if (target_ == records_)
        {
            return {resource::Status::Ok, original_, 0, true};
        }
        return {resource::Status::Ok, pack(ordinal_, offset_), static_cast<std::uint32_t>(used_),
                !stopped_ && ordinal_ == records_};
    }

private:
    bool pause() noexcept
    {
        stopped_ = true;
        if (used_ == 0)
        {
            status_ = resource::Status::BufferTooSmall;
        }
        return false;
    }

    resource::Output out_;
    resource::Cursor original_;
    std::size_t used_ = 0;
    std::uint32_t ordinal_ = 0;
    std::uint32_t target_;
    std::uint32_t offset_;
    std::uint32_t records_;
    resource::Status status_ = resource::Status::Ok;
    bool stopped_ = false;
};
} // namespace telemetry_resource::detail
