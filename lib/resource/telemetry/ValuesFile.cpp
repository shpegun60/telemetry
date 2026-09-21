/**
 * @file ValuesFile.cpp
 * @brief Canonical fixed-width scalar bits with atomic live-value emission.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#include "ValuesFile.hpp"
#include "detail/Stream.hpp"
#include <bit>
#include <variant>

namespace telemetry_resource
{
namespace
{
using detail::Stream;
using detail::Writer;

// Payload hex is most-significant digit first, independent of machine endian.
// Status 00 means a value; 01 means Null/unavailable, followed by zero payload.
constexpr unsigned payloadBytes(telemetry::ScalarType type) noexcept
{
    using T = telemetry::ScalarType;
    switch (type)
    {
        case T::Null:
            return 0;
        case T::U8:
        case T::S8:
        case T::Bool:
            return 1;
        case T::U16:
        case T::S16:
            return 2;
        case T::U32:
        case T::S32:
        case T::F32:
            return 4;
        case T::U64:
        case T::S64:
        case T::F64:
            return 8;
    }
    return 0;
}

std::uint64_t bits(const telemetry::Scalar& value) noexcept
{
    static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);
    return value.visit(
        [](auto number) noexcept -> std::uint64_t
        {
            using T = decltype(number);
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return 0;
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                return std::bit_cast<std::uint32_t>(number);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return std::bit_cast<std::uint64_t>(number);
            }
            else
            {
                return static_cast<std::uint64_t>(number);
            }
        });
}

// At most eight hex digits per word: F32/small integers never pay for a
// variable 64-bit shift on ARM. F64/U64/S64 use two native-width words.
void hexWord(std::byte* out, std::uint32_t word, unsigned digits) noexcept
{
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned d = digits; d != 0; --d)
    {
        *out++ = std::byte(hex[(word >> (4 * (d - 1))) & 15]);
    }
}

void valueToken(resource::Output out, const telemetry::Field& field, bool comma) noexcept
{
    const auto value = field.read();
    const auto word = bits(value);
    const unsigned digits = 2 * payloadBytes(field.readType);
    unsigned n = 0;
    if (comma)
    {
        out[n++] = std::byte{','};
    }
    out[n++] = std::byte{'"'};
    out[n++] = std::byte{'0'};
    out[n++] = value.type() == telemetry::ScalarType::Null ? std::byte{'1'} : std::byte{'0'};
    if (digits == 16)
    {
        hexWord(out.data() + n, static_cast<std::uint32_t>(word >> 32), 8);
        hexWord(out.data() + n + 8, static_cast<std::uint32_t>(word), 8);
    }
    else
    {
        hexWord(out.data() + n, static_cast<std::uint32_t>(word), digits);
    }
    out[n + digits] = std::byte{'"'};
}

bool emit(const telemetry::CatalogIndex& index, Stream& stream) noexcept
{
    if (!stream.record(
            [](Writer& out) noexcept
            {
                return out.text("{");
            }))
    {
        return false;
    }
    for (const auto catalog : index.catalogs())
    {
        const auto entries = catalog.fields();
        if (stream.skip(entries.size() + 2))
        {
            continue;
        }
        if (!stream.record(
                [&](Writer& out) noexcept
                {
                    return (catalog.index() == 0 || out.text(",")) &&
                           out.requiredString(catalog.name()) && out.text(":[");
                }))
        {
            return false;
        }
        const auto start = stream.skipEntries(entries.size());
        for (std::size_t i = start; i < entries.size(); ++i)
        {
            const auto& field = catalog.catalog().fields[i];
            const auto width = 4 + 2 * payloadBytes(field.readType) + (i == 0 ? 0 : 1);
            if (!stream.atomic(width,
                               [&](resource::Output out) noexcept
                               {
                                   valueToken(out, field, i != 0);
                               }))
            {
                return false;
            }
        }
        if (!stream.record(
                [](Writer& out) noexcept
                {
                    return out.text("]");
                }))
        {
            return false;
        }
    }
    return stream.record(
        [](Writer& out) noexcept
        {
            return out.text("}");
        });
}
} // namespace

ValuesFile::ValuesFile(const telemetry::CatalogIndex& index,
                       telemetry::detail::CurrentAbiTag) noexcept
    : catalogs_(index.data()), count_(index.size())
{
    Stream measure;
    if (emit(index, measure))
    {
        size_ = measure.size();
        records_ = measure.records();
    }
}

resource::ReadResult ValuesFile::read(resource::Cursor cursor, resource::Output out) const noexcept
{
    if (size_ == 0)
    {
        return {resource::Status::InvalidData, cursor};
    }
    Stream stream{cursor, out};
    (void)emit(telemetry::CatalogIndex{catalogs_, count_}, stream);
    return stream.result(records_);
}
} // namespace telemetry_resource
