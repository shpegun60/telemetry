/**
 * @file BinaryWriter.hpp
 * @brief Bounded binary writer and checked measuring/hash sink sharing one encoder.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include "BinaryScalar.hpp"
#include "Fingerprint.hpp"
#include <telemetry/core/TelemetryCompiler.h>
#include <resource/Types.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace telemetry_resource::detail
{
// Numeric and string wire primitives have one implementation. CRTP selects
// counting/hash or bounded output at compile time; no virtual dispatch or mode
// flag is needed in the READ path.
template <class Derived>
class WireWriter
{
    Derived& sink_() noexcept
    {
        return static_cast<Derived&>(*this);
    }

public:
    bool raw(std::string_view value) noexcept
    {
        return sink_().bytes(std::as_bytes(std::span{value.data(), value.size()}));
    }

    bool string(std::string_view value) noexcept
    {
        return value.size() <= UINT32_MAX
                   ? u32(static_cast<std::uint32_t>(value.size())) && raw(value)
                   : sink_().fail();
    }

    bool u8(std::uint8_t value) noexcept
    {
        const auto byte = static_cast<std::byte>(value);
        return sink_().bytes({&byte, 1});
    }

    bool u16(std::uint16_t value) noexcept
    {
        const std::byte data[]{std::byte(value & 255u), std::byte(value >> 8)};
        return sink_().bytes(data);
    }

    bool u32(std::uint32_t value) noexcept
    {
        const std::byte data[]{std::byte(value & 255u), std::byte((value >> 8) & 255u),
                               std::byte((value >> 16) & 255u), std::byte(value >> 24)};
        return sink_().bytes(data);
    }

    bool u64(std::uint64_t value) noexcept
    {
        return u32(static_cast<std::uint32_t>(value)) &&
               u32(static_cast<std::uint32_t>(value >> 32));
    }

    bool scalar(const telemetry::Scalar& value) noexcept
    {
        const auto type = toWireType(value.type());
        if (type == invalidWireType)
        {
            return sink_().fail();
        }
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
};

// Construction-only measuring/hash sink. READ never instantiates this type.
class BinaryWriter : public WireWriter<BinaryWriter>
{
public:
    BinaryWriter() noexcept = default;

    explicit BinaryWriter(Fingerprint& hash) noexcept : hash_(&hash)
    {
    }

    bool bytes(resource::Input value) noexcept
    {
        if (!ok_ || value.size() > UINT32_MAX - count_)
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

    bool fail() noexcept
    {
        ok_ = false;
        return false;
    }

    bool ok() const noexcept
    {
        return ok_;
    }

    std::uint32_t count() const noexcept
    {
        return count_;
    }

private:
    Fingerprint* hash_ = nullptr;
    std::uint32_t count_ = 0;
    bool ok_ = true;
};

// READ-only sink. Only output bounds and local byte skip survive in RAM.
class OutputWriter : public WireWriter<OutputWriter>
{
public:
    explicit OutputWriter(resource::Output out, std::uint32_t skip = 0) noexcept
        : out_(out), skip_(skip)
    {
    }

    TELEMETRY_FORCE_INLINE bool bytes(resource::Input value) noexcept
    {
        if (!ok_ || full_)
        {
            return false;
        }
        const auto skipped = std::min<std::size_t>(skip_, value.size());
        skip_ -= static_cast<std::uint32_t>(skipped);
        value = value.subspan(skipped);
        const auto copied = std::min(value.size(), out_.size());
        if (copied != 0)
        {
            std::memcpy(out_.data(), value.data(), copied);
        }
        if (copied != 0)
        {
            out_ = out_.subspan(copied);
        }
        full_ = copied != value.size();
        return !full_;
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

    std::size_t remaining() const noexcept
    {
        return out_.size();
    }

    std::uint32_t skip() const noexcept
    {
        return skip_;
    }

private:
    resource::Output out_;
    std::uint32_t skip_;
    bool ok_ = true, full_ = false;
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
