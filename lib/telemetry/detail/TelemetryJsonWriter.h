/**
 * @file TelemetryJsonWriter.h
 * @brief Internal bounded JSON text writer without telemetry dependencies.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 *
 * This header is an implementation detail and is not a stable public API.
 */
#ifndef TELEMETRY_DETAIL_JSON_WRITER_H
#define TELEMETRY_DETAIL_JSON_WRITER_H

#include "../core/TelemetryCompiler.h"

#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

namespace telemetry {
namespace detail {

// Bounded append; returns false permanently once anything does not fit.
class JsonWriter {
public:
    JsonWriter(char* const buffer, const std::size_t size,
               const bool quoteInt64 = false) noexcept
        : buffer_(buffer), size_(size), ok_(buffer != nullptr && size != 0),
          quoteInt64_(quoteInt64)
    {
        if (ok_) buffer_[0] = '\0';
    }

    bool append(const char* text) noexcept
    {
        return appendText_(text, std::strlen(text));
    }

    template <typename First, typename... Args>
    bool append(const char* format, First first, Args... args) noexcept
    {
        if (!ok_ || (offset_ >= size_)) {
            ok_ = false;
            return false;
        }
        const int written =
            std::snprintf(buffer_ + offset_, size_ - offset_, format, first, args...);
        if ((written < 0) || (static_cast<std::size_t>(written) >= (size_ - offset_))) {
            buffer_[offset_] = '\0';
            ok_ = false;
            return false;
        }
        offset_ += static_cast<std::size_t>(written);
        return true;
    }

    bool appendFloating(double number, int precision) noexcept
    {
        if (!ok_) return false;
        // %g uses the current decimal separator. Format locally, then replace
        // that separator without changing the application's numeric locale.
        char text[64];
        const int written = std::snprintf(text, sizeof(text), "%.*g", precision, number);
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(text)) {
            ok_ = false;
            return false;
        }
        const char* const decimal = std::localeconv()->decimal_point;
        if (std::strcmp(decimal, ".") != 0 && decimal[0] != '\0') {
            if (const char* const point = std::strstr(text, decimal)) {
                return appendText_(text, static_cast<std::size_t>(point - text))
                    && append(".") && append(point + std::strlen(decimal));
            }
        }
        return appendText_(text, static_cast<std::size_t>(written));
    }

    bool appendInteger(std::uint64_t number) noexcept { return appendDecimal_(number, false); }

    bool appendInteger(std::int64_t number) noexcept
    {
        const bool negative = number < 0;
        // Unsigned subtraction also handles INT64_MIN without signed overflow.
        const auto bits = static_cast<std::uint64_t>(number);
        return appendDecimal_(negative ? std::uint64_t{0} - bits : bits, negative);
    }

    bool appendQuotedInteger(std::uint64_t number) noexcept
    {
        return append("\"") && appendInteger(number) && append("\"");
    }

    bool appendQuotedInteger(std::int64_t number) noexcept
    {
        return append("\"") && appendInteger(number) && append("\"");
    }

    bool appendString(std::string_view text) noexcept
    {
        if (!append("\"")) return false;
        std::size_t first = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte >= 0x20 && byte != '"' && byte != '\\') continue;
            if (i != first && !appendText_(text.data() + first, i - first)) return false;
            if (byte == '"' || byte == '\\') {
                if (!append("\\%c", static_cast<int>(byte))) return false;
            } else if (!append("\\u%04x", static_cast<unsigned>(byte))) return false;
            first = i + 1;
        }
        if (first != text.size() && !appendText_(text.data() + first, text.size() - first)) return false;
        return append("\"");
    }

    TELEMETRY_FORCE_INLINE bool appendRequiredString(const char* text) noexcept
    {
        if (text == nullptr) {
            ok_ = false;
            return false;
        }
        return appendString(std::string_view{text});
    }

    bool ok() const noexcept { return ok_; }
    bool quoteInt64() const noexcept { return quoteInt64_; }
    std::size_t length() const noexcept { return ok_ ? offset_ : 0u; }

private:
    bool appendDecimal_(std::uint64_t number, bool negative) noexcept
    {
        // Newlib-nano need not support printf's long-long format. Keep the
        // full integer value independent of that optional C library feature.
        char text[std::numeric_limits<std::uint64_t>::digits10 + 2];
        char* const end = text + sizeof(text);
        char* first = end;
        do {
            *--first = static_cast<char>('0' + number % 10);
            number /= 10;
        } while (number != 0);
        if (negative) *--first = '-';
        return appendText_(first, static_cast<std::size_t>(end - first));
    }

    bool appendText_(const char* text, std::size_t length) noexcept
    {
        if (!ok_ || length >= size_ - offset_) {
            ok_ = false;
            return false;
        }
        std::memcpy(buffer_ + offset_, text, length);
        offset_ += length;
        buffer_[offset_] = '\0';
        return true;
    }

    char* buffer_;
    std::size_t size_;
    std::size_t offset_ = 0u;
    bool ok_;
    bool quoteInt64_;
};

} // namespace detail
} // namespace telemetry

#endif
