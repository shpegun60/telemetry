/**
 * @file Stream.hpp
 * @brief Internal record cursor, counting sink and bounded partial text writer.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "FloatText.hpp"
#include <resource/Types.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

namespace telemetry_resource::detail
{
// A record is a root/catalog delimiter, a field/command description, or one
// whole live-value token. Its ordinal and byte offset fit independently in u32:
// the complete file fits FileSize and every record emits at least one byte.
constexpr resource::Cursor pack(std::uint32_t record, std::uint32_t offset) noexcept
{
    return (resource::Cursor{record} << 32) | offset;
}

static_assert(sizeof(resource::Cursor) * 8 >= 2 * 32);

// One emission pass has two modes: count its full length, or skip an already
// transmitted prefix and copy only what fits in the caller's current buffer.
// A full buffer is a normal pause; ok_ is reserved for invalid metadata/length.
class Writer
{
public:
    Writer() noexcept : counting_(true)
    {
    }

    Writer(resource::Output out, std::uint32_t skip) noexcept : out_(out), skip_(skip)
    {
    }

    bool text(std::string_view value) noexcept
    {
        if (!ok_)
        {
            return false;
        }
        if (counting_)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max() - count_)
            {
                return fail();
            }
            count_ += value.size();
            return true;
        }
        // Resume inside this record without storing any of its text between
        // calls. The skipped bytes are counted but never copied to output.
        const auto skipped = std::min<std::size_t>(skip_, value.size());
        skip_ -= static_cast<std::uint32_t>(skipped);
        value.remove_prefix(skipped);
        const auto copied = std::min(value.size(), out_.size() - used_);
        if (copied != 0)
        {
            std::memcpy(out_.data() + used_, value.data(), copied);
        }
        used_ += copied;
        if (copied != value.size())
        {
            full_ = true;
            return false;
        }
        return true;
    }

    bool requiredString(const char* value) noexcept
    {
        if (value == nullptr)
        {
            return fail();
        }
        // Avoid even strlen when no part of the next token can be emitted.
        return readyForToken() && string(value);
    }

    bool string(std::string_view value) noexcept
    {
        if (!text("\""))
        {
            return false;
        }
        std::size_t first = 0;
        while (first < value.size())
        {
            std::size_t end = value.size();
            if (!counting_)
            {
                // Escaping never shortens the input. Inspect at most the raw
                // prefix that could be skipped or copied during this call.
                // Subtract first so skip + capacity cannot overflow size_t.
                const auto skipped = std::min<std::size_t>(skip_, value.size() - first);
                const auto copied = std::min(value.size() - first - skipped, out_.size() - used_);
                end = first + skipped + copied;
            }
            auto i = first;
            for (; i < end; ++i)
            {
                const auto byte = static_cast<unsigned char>(value[i]);
                if (byte < 0x20 || byte == '"' || byte == '\\')
                {
                    break;
                }
            }
            if (i == end)
            {
                // Only the inspected prefix can fit. text() pauses before
                // copying any uninspected suffix or attempting the quote.
                return text(value.substr(first)) && text("\"");
            }
            const auto b = static_cast<unsigned char>(value[i]);
            if (!text(value.substr(first, i - first)))
            {
                return false;
            }
            if (b == '"' || b == '\\')
            {
                const char escaped[2] = {'\\', static_cast<char>(b)};
                if (!text({escaped, 2}))
                {
                    return false;
                }
            }
            else
            {
                const char* hex = "0123456789abcdef";
                const char escaped[6] = {'\\', 'u', '0', '0', hex[b >> 4], hex[b & 15]};
                if (!text({escaped, 6}))
                {
                    return false;
                }
            }
            first = i + 1;
        }
        return text("\"");
    }

    bool hex32(std::uint32_t value) noexcept
    {
        if (!readyForToken())
        {
            return false;
        }
        char buffer[8];
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < 8; ++i)
        {
            buffer[i] = hex[(value >> (4 * (7 - i))) & 15];
        }
        return text({buffer, sizeof(buffer)});
    }

    template <class T>
    bool integer(T value) noexcept
    {
        static_assert(std::is_integral_v<T>);
        if (!readyForToken())
        {
            return false;
        }
        bool negative = false;
        if constexpr (std::is_signed_v<T>)
        {
            negative = value < 0;
        }
        // Preserve native-width division on ARM for IDs, flags and small
        // scalar types. Only actual 64-bit values need 64-bit arithmetic.
        using Magnitude =
            std::conditional_t<(sizeof(T) <= sizeof(std::uint32_t)), std::uint32_t, std::uint64_t>;
        auto magnitude = static_cast<Magnitude>(value);
        if (negative)
        {
            magnitude = Magnitude{0} - magnitude;
        }
        char buffer[std::numeric_limits<Magnitude>::digits10 + 2];
        char* end = buffer + sizeof(buffer);
        char* first = end;
        do
        {
            *--first = static_cast<char>('0' + magnitude % 10);
            magnitude /= 10;
        } while (magnitude);
        if (negative)
        {
            *--first = '-';
        }
        return text({first, static_cast<std::size_t>(end - first)});
    }

    bool floating(double value) noexcept
    {
        if (!readyForToken())
        {
            return false;
        }
        char buffer[32];
        const auto length = floatingText(value, buffer);
        if (length == 0)
        {
            return fail();
        }
        return text({buffer, length});
    }

    bool fail() noexcept
    {
        ok_ = false;
        return false;
    }

    bool ok() const noexcept
    {
        return ok_;
    }

    bool full() const noexcept
    {
        return full_;
    }

    std::uint32_t skip() const noexcept
    {
        return skip_;
    }

    std::size_t used() const noexcept
    {
        return used_;
    }

    std::uint32_t count() const noexcept
    {
        return static_cast<std::uint32_t>(count_);
    }

private:
    bool readyForToken() noexcept
    {
        if (!ok_)
        {
            return false;
        }
        if (!counting_ && skip_ == 0 && used_ == out_.size())
        {
            full_ = true;
            return false;
        }
        return true;
    }

    resource::Output out_{};
    std::size_t used_ = 0;   // Bytes committed to this output span.
    std::size_t count_ = 0;  // Full record length in counting mode.
    std::uint32_t skip_ = 0; // Unconsumed part of the resume offset.
    bool counting_ = false;
    bool ok_ = true;
    bool full_ = false;
};

// Drives the same ordered record emitter during construction and each read.
// The provider retains only size/record counts; the caller owns continuation.
class Stream
{
public:
    Stream() noexcept : counting_(true)
    {
    }

    Stream(resource::Cursor cursor, resource::Output out) noexcept
        : out_(out.first(
              std::min<std::size_t>(out.size(), std::numeric_limits<std::uint32_t>::max()))),
          original_(cursor), next_(cursor), target_(static_cast<std::uint32_t>(cursor >> 32)),
          offset_(static_cast<std::uint32_t>(cursor))
    {
    }

    // Skip complete catalogs/entry prefixes arithmetically, without formatting
    // their JSON or revisiting getters. Only catalog counts are inspected.
    bool skip(std::size_t records) noexcept
    {
        if (!counting_ && target_ >= ordinal_ && records <= target_ - ordinal_)
        {
            ordinal_ += static_cast<std::uint32_t>(records);
            return true;
        }
        return false;
    }

    std::size_t skipEntries(std::size_t count) noexcept
    {
        if (counting_ || target_ < ordinal_)
        {
            return 0;
        }
        const auto skipped = std::min<std::size_t>(count, target_ - ordinal_);
        ordinal_ += static_cast<std::uint32_t>(skipped);
        return skipped;
    }

    template <class Emit>
    bool record(Emit emit) noexcept
    {
        // Construction validates and measures complete records. The same
        // callback supplies the bytes later, so size cannot drift by formula.
        if (counting_)
        {
            Writer writer;
            if (!emit(writer) || !writer.ok() || writer.count() == 0)
            {
                return fail(resource::Status::InvalidData);
            }
            return measured(writer.count());
        }
        if (ordinal_ < target_)
        {
            ++ordinal_;
            return true;
        }
        // The callback is synchronous. Its temporary strings and parameter
        // descriptions are consumed here and never retained in cursor state.
        Writer writer{out_.subspan(used_), offset_};
        const bool done = emit(writer);
        if (!writer.ok() || (!done && !writer.full()))
        {
            return fail(resource::Status::InvalidData);
        }
        // Reaching the record end before consuming the requested offset means
        // the caller supplied a cursor outside this record.
        if (done && writer.skip() != 0)
        {
            return fail(resource::Status::InvalidCursor);
        }
        used_ += writer.used();
        if (done)
        {
            // A completed record always resumes at byte zero of the next one.
            ++ordinal_;
            offset_ = 0;
            next_ = pack(ordinal_, 0);
            return true;
        }
        // A full output span pauses within the current immutable record.
        next_ = pack(ordinal_, offset_ + static_cast<std::uint32_t>(writer.used()));
        stopped_ = true;
        if (used_ == 0)
        {
            status_ = resource::Status::BufferTooSmall;
        }
        return false;
    }

    template <class Emit>
    bool atomic(std::uint32_t width, Emit emit) noexcept
    {
        static_assert(std::is_same_v<decltype(emit(resource::Output{})), void> &&
                      noexcept(emit(resource::Output{})));
        // Measuring or skipping live values never evaluates their callback.
        if (counting_)
        {
            return width != 0 ? measured(width) : fail(resource::Status::InvalidData);
        }
        if (ordinal_ < target_)
        {
            ++ordinal_;
            return true;
        }
        if (offset_ != 0)
        {
            return fail(resource::Status::InvalidCursor);
        }
        // Check the entire token before calling emit(), which may read a live
        // getter. This includes quotes, status, payload and its leading comma.
        if (width > out_.size() - used_)
        {
            stopped_ = true;
            next_ = pack(ordinal_, 0);
            if (used_ == 0)
            {
                status_ = resource::Status::BufferTooSmall;
            }
            return false;
        }
        // Width and cursor are already validated. Emit directly into exactly
        // this token's reserved span, without an intermediate Writer or copy.
        emit(out_.subspan(used_, width));
        used_ += width;
        next_ = pack(++ordinal_, 0);
        return true;
    }

    bool fail(resource::Status status) noexcept
    {
        status_ = status;
        return false;
    }

    bool ok() const noexcept
    {
        return status_ == resource::Status::Ok;
    }

    std::uint32_t size() const noexcept
    {
        return ok() ? total_ : 0;
    }

    std::uint32_t records() const noexcept
    {
        return ok() ? ordinal_ : 0;
    }

    resource::ReadResult result(std::uint32_t records) const noexcept
    {
        // Errors commit no bytes and preserve the caller's original cursor.
        // Output may contain a discarded prefix, but written remains zero.
        if (status_ != resource::Status::Ok)
        {
            return {status_, original_};
        }
        if (target_ > records || (target_ == records && offset_ != 0))
        {
            return {resource::Status::InvalidCursor, original_};
        }
        return {resource::Status::Ok, next_, static_cast<std::uint32_t>(used_),
                !stopped_ && ordinal_ == records};
    }

private:
    bool measured(std::uint32_t count) noexcept
    {
        if (count > std::numeric_limits<std::uint32_t>::max() - total_ ||
            ordinal_ == std::numeric_limits<std::uint32_t>::max())
        {
            return fail(resource::Status::InvalidData);
        }
        total_ += count;
        ++ordinal_;
        return true;
    }

    resource::Output out_{};
    resource::Cursor original_ = 0;
    resource::Cursor next_ = 0;
    std::size_t used_ = 0; // Total bytes emitted by this read.
    // Construction checks both against u32; every record occupies bytes in
    // a u32-sized file. Keep their arithmetic native-width on 32-bit MCUs.
    std::uint32_t ordinal_ = 0; // Record currently visited by the emitter.
    std::uint32_t total_ = 0;   // Checked file length in counting mode.
    std::uint32_t target_ = 0;  // First record requested by the caller.
    std::uint32_t offset_ = 0;  // Byte offset within the resumed record.
    resource::Status status_ = resource::Status::Ok;
    bool counting_ = false;
    bool stopped_ = false;
};
} // namespace telemetry_resource::detail
