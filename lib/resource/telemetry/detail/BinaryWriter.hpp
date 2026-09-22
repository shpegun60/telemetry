/**
 * @file BinaryWriter.hpp
 * @brief Bounded binary writer and checked measuring/hash sink sharing one encoder.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "BinaryScalar.hpp"
#include "Fingerprint.hpp"
#include <resource/Types.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace telemetry_resource::detail
{
class BinaryWriter
{
public:
    BinaryWriter() noexcept = default; // Measure only; never touches payload callbacks.

    explicit BinaryWriter(Fingerprint& hash) noexcept : hash_(&hash)
    {
    }

    BinaryWriter(resource::Output out, std::uint32_t skip = 0) noexcept
        : out_(out), skip_(skip), measuring_(false)
    {
    }

    bool bytes(resource::Input value) noexcept
    {
        if (!ok_ || full_)
        {
            return false;
        }
        if (measuring_)
        {
            if (value.size() > UINT32_MAX - count_)
            {
                return fail();
            }
            count_ += static_cast<std::uint32_t>(value.size());
            if (hash_ != nullptr)
            {
                hash_->bytes(value);
            }
            return true;
        }
        const auto skipped = std::min<std::size_t>(skip_, value.size());
        skip_ -= static_cast<std::uint32_t>(skipped);
        value = value.subspan(skipped);
        const auto copied = std::min(value.size(), out_.size() - used_);
        if (copied != 0)
        {
            std::memcpy(out_.data() + used_, value.data(), copied);
        }
        used_ += copied;
        full_ = copied != value.size();
        return !full_;
    }

    bool raw(std::string_view value) noexcept
    {
        return bytes(std::as_bytes(std::span{value.data(), value.size()}));
    }

    bool string(std::string_view value) noexcept
    {
        return value.size() <= UINT32_MAX
                   ? u32(static_cast<std::uint32_t>(value.size())) && raw(value)
                   : fail();
    }

    bool u8(std::uint8_t value) noexcept
    {
        const auto byte = static_cast<std::byte>(value);
        return bytes({&byte, 1});
    }

    bool u16(std::uint16_t value) noexcept
    {
        const std::byte data[]{std::byte(value & 255u), std::byte(value >> 8)};
        return bytes(data);
    }

    bool u32(std::uint32_t value) noexcept
    {
        const std::byte data[]{std::byte(value & 255u), std::byte((value >> 8) & 255u),
                               std::byte((value >> 16) & 255u), std::byte(value >> 24)};
        return bytes(data);
    }

    bool u64(std::uint64_t value) noexcept
    {
        return u32(static_cast<std::uint32_t>(value)) &&
               u32(static_cast<std::uint32_t>(value >> 32));
    }

    bool scalar(const telemetry::Scalar& value) noexcept
    {
        const auto type = toWireType(value.type());
        const auto width = payloadSize(type);
        if (!u8(static_cast<std::uint8_t>(type)) ||
            !u8(static_cast<std::uint8_t>(width == 0 ? ScalarState::Null : ScalarState::Value)) ||
            !u8(width))
        {
            return false;
        }
        if (width == 0)
        {
            return true;
        }
        const auto bits = scalarBits(value);
        switch (width)
        {
            case 1:
                return u8(static_cast<std::uint8_t>(bits));
            case 2:
                return u16(static_cast<std::uint16_t>(bits));
            case 4:
                return u32(static_cast<std::uint32_t>(bits));
            default:
                return u64(bits);
        }
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

    std::size_t used() const noexcept
    {
        return used_;
    }

    std::uint32_t count() const noexcept
    {
        return count_;
    }

    std::uint32_t skip() const noexcept
    {
        return skip_;
    }

private:
    resource::Output out_{};
    Fingerprint* hash_ = nullptr;
    std::size_t used_ = 0;
    std::uint32_t count_ = 0;
    std::uint32_t skip_ = 0;
    bool measuring_ = true;
    bool ok_ = true;
    bool full_ = false;
};

// Construction visits metadata once. Semantic hash records are postorder:
// children before parent, each [type, version=1, flags=0, payload, payloadSize].
// Wire order is parent first. This needs neither stored dictionaries nor a
// second visitor pass just to discover the parent's child count.
struct MetadataMeasure
{
    Fingerprint hash;
    std::uint32_t size = metadataHeaderSize;
    std::uint32_t records = 0;
    bool valid = true;

    explicit MetadataMeasure(std::string_view magic) noexcept
    {
        BinaryWriter out{hash};
        valid = out.raw(magic) && out.u16(binaryMajor) && out.u16(binaryMinor);
    }

    template <class Emit>
    bool record(std::uint8_t type, Emit emit) noexcept
    {
        if (!valid)
        {
            return false;
        }
        BinaryWriter out{hash};
        valid = out.u8(type) && out.u8(1) && out.u16(0) && emit(out);
        const auto payload = out.count() - 4;
        valid = valid && out.ok() && out.u32(payload) && size <= UINT32_MAX - recordHeaderSize &&
                payload <= UINT32_MAX - size - recordHeaderSize;
        if (valid)
        {
            size += recordHeaderSize + payload;
            ++records;
        }
        return valid;
    }
};
} // namespace telemetry_resource::detail
