/**
 * @file FloatText.hpp
 * @brief Bounded, table-free binary64 formatting for 17-digit JSON metadata.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../../LICENSE.
 */
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace telemetry_resource::detail
{
namespace float_text
{
// Store the exact decimal integer behind m * 2^e. For a negative exponent,
// m * 5^(-e) * 10^e is the same number. Binary64 needs fewer than 768 decimal
// digits: m < 10^16 and 5^1074 < 10^752. Positive exponents need at most 309.
// Base 10^4 fits each limb in uint16_t. Multiplication by 5^8 or 2^18
// remains within uint32_t, so scaling consumes several exponent bits per pass
// without software 64-bit division. The workspace is still only 384 bytes.
class Decimal
{
public:
    explicit Decimal(std::uint64_t value) noexcept
    {
        // Decode the few high limbs with wide division, then use the MCU's
        // native width for the remaining digits (and for ordinary integers).
        while (value > std::numeric_limits<std::uint32_t>::max())
        {
            parts_[used_++] = static_cast<std::uint16_t>(value % base);
            value /= base;
        }
        auto remaining = static_cast<std::uint32_t>(value);
        do
        {
            parts_[used_++] = static_cast<std::uint16_t>(remaining % base);
            remaining /= base;
        } while (remaining != 0);
    }

    bool multiply(std::uint32_t factor) noexcept
    {
        std::uint32_t carry = 0;
        for (unsigned i = 0; i < used_; ++i)
        {
            const std::uint32_t product = parts_[i] * factor + carry;
            parts_[i] = static_cast<std::uint16_t>(product % base);
            carry = product / base;
        }
        while (carry != 0)
        {
            if (used_ == parts_.size())
            {
                return false;
            }
            parts_[used_++] = static_cast<std::uint16_t>(carry % base);
            carry /= base;
        }
        return true;
    }

    unsigned size() const noexcept
    {
        return used_;
    }

    std::uint32_t part(unsigned index) const noexcept
    {
        return parts_[index];
    }

    unsigned leadingDigits() const noexcept
    {
        unsigned digits = 1;
        for (auto leading = parts_[used_ - 1]; leading >= 10; leading /= 10)
        {
            ++digits;
        }
        return digits;
    }

    static constexpr std::uint32_t base = 10000;
    static constexpr unsigned digitsPerPart = 4;

private:
    static_assert(std::uint64_t{base} * 390625 <= std::numeric_limits<std::uint32_t>::max());
    // Only [0, used_) is initialized and accessed. Avoid clearing unused
    // workspace for the common small-number case.
    std::array<std::uint16_t, 192> parts_;
    unsigned used_ = 0;
};
} // namespace float_text

// The representation matches general format with precision 17 and nearest,
// ties-to-even rounding. No floating arithmetic, locale, allocation or lookup
// tables are involved. A 32-byte output covers the maximum 24-byte result.
// Non-finite input returns zero; the JSON layer emits those values as null.
inline std::size_t floatingText(double value, char (&output)[32]) noexcept
{
    static_assert(sizeof(double) == sizeof(std::uint64_t) &&
                  std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53);
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const unsigned encodedExponent = static_cast<unsigned>((bits >> 52) & 0x7ff);
    if (encodedExponent == 0x7ff)
    {
        return 0;
    }

    std::size_t written = 0;
    if ((bits >> 63) != 0)
    {
        output[written++] = '-';
    }
    std::uint64_t significand = bits & ((std::uint64_t{1} << 52) - 1);
    int exponent = -1074;
    if (encodedExponent != 0)
    {
        significand |= std::uint64_t{1} << 52;
        exponent = static_cast<int>(encodedExponent) - 1023 - 52;
    }
    if (significand == 0)
    {
        output[written++] = '0';
        return written;
    }

    // Cancel powers of two before constructing the exact decimal expansion.
    // Integers and simple fractions often reduce to only a few small limbs.
    const int trailing = std::countr_zero(significand);
    significand >>= trailing;
    exponent += trailing;
    float_text::Decimal decimal{significand};
    const int scale = exponent < 0 ? exponent : 0;
    if (exponent < 0)
    {
        for (; exponent <= -8; exponent += 8)
        {
            if (!decimal.multiply(390625)) // 5^8
            {
                return 0;
            }
        }
        std::uint32_t factor = 1;
        for (; exponent < 0; ++exponent)
        {
            factor *= 5;
        }
        if (factor != 1 && !decimal.multiply(factor))
        {
            return 0;
        }
    }
    else
    {
        for (; exponent >= 18; exponent -= 18)
        {
            if (!decimal.multiply(std::uint32_t{1} << 18))
            {
                return 0;
            }
        }
        if (exponent != 0 && !decimal.multiply(std::uint32_t{1} << exponent))
        {
            return 0;
        }
    }

    const unsigned leading = decimal.leadingDigits();
    int decimalExponent =
        static_cast<int>((decimal.size() - 1) * float_text::Decimal::digitsPerPart + leading) - 1 +
        scale;
    char digits[17];
    unsigned count = 0;
    unsigned guard = 0;
    bool sticky = false;
    bool rounded = false;
    for (unsigned part = decimal.size(); part != 0 && !rounded;)
    {
        --part;
        auto number = decimal.part(part);
        std::uint32_t divisor = 1;
        const unsigned width =
            part == decimal.size() - 1 ? leading : float_text::Decimal::digitsPerPart;
        for (unsigned i = 1; i < width; ++i)
        {
            divisor *= 10;
        }
        for (; divisor != 0; divisor /= 10)
        {
            const auto digit = number / divisor;
            number %= divisor;
            if (count < sizeof(digits))
            {
                digits[count++] = static_cast<char>('0' + digit);
            }
            else
            {
                // After the first discarded digit, only a nonzero-tail bit
                // is needed. There is no reason to format the remaining limbs.
                guard = digit;
                sticky = number != 0;
                while (part != 0 && !sticky)
                {
                    sticky = decimal.part(--part) != 0;
                }
                rounded = true;
                break;
            }
        }
    }
    if (guard > 5 || (guard == 5 && (sticky || ((digits[count - 1] - '0') & 1))))
    {
        unsigned position = count;
        while (position != 0 && digits[position - 1] == '9')
        {
            digits[--position] = '0';
        }
        if (position == 0)
        {
            digits[0] = '1';
            ++decimalExponent;
        }
        else
        {
            ++digits[position - 1];
        }
    }
    while (count > 1 && digits[count - 1] == '0')
    {
        --count;
    }

    if (decimalExponent < -4 || decimalExponent >= 17)
    {
        output[written++] = digits[0];
        if (count > 1)
        {
            output[written++] = '.';
            for (unsigned i = 1; i < count; ++i)
            {
                output[written++] = digits[i];
            }
        }
        output[written++] = 'e';
        output[written++] = decimalExponent < 0 ? '-' : '+';
        const unsigned magnitude =
            static_cast<unsigned>(decimalExponent < 0 ? -decimalExponent : decimalExponent);
        if (magnitude >= 100)
        {
            output[written++] = static_cast<char>('0' + magnitude / 100);
        }
        output[written++] = static_cast<char>('0' + (magnitude / 10) % 10);
        output[written++] = static_cast<char>('0' + magnitude % 10);
    }
    else if (decimalExponent < 0)
    {
        output[written++] = '0';
        output[written++] = '.';
        for (int i = -1; i > decimalExponent; --i)
        {
            output[written++] = '0';
        }
        for (unsigned i = 0; i < count; ++i)
        {
            output[written++] = digits[i];
        }
    }
    else
    {
        const auto point = static_cast<unsigned>(decimalExponent + 1);
        for (unsigned i = 0; i < count || i < point; ++i)
        {
            if (i == point)
            {
                output[written++] = '.';
            }
            output[written++] = i < count ? digits[i] : '0';
        }
    }
    return written;
}
} // namespace telemetry_resource::detail
