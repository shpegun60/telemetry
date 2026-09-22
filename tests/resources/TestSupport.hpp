// Shared bounded transfer and independent binary-reader checks (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#pragma once
#include <resource/Types.hpp>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>
inline unsigned checks = 0;
#define CHECK(...)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(__VA_ARGS__))                                                                        \
        {                                                                                          \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__);                         \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
using Bytes = std::vector<std::byte>;

inline Bytes unhex(std::string_view s)
{
    Bytes out;
    unsigned value = 0, digits = 0;
    for (char c : s)
    {
        if (c == ' ' || c == '\n')
        {
            continue;
        }
        CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
        if (++digits == 2)
        {
            out.push_back(std::byte(value));
            value = digits = 0;
        }
    }
    CHECK(digits == 0);
    return out;
}

struct Reader
{
    std::span<const std::byte> bytes;
    std::size_t offset = 0;

    std::uint64_t number(unsigned width)
    {
        CHECK(width <= 8 && width <= bytes.size() - offset);
        std::uint64_t result = 0;
        for (unsigned i = 0; i < width; ++i)
        {
            result |= std::uint64_t(std::to_integer<unsigned>(bytes[offset++])) << (i * 8);
        }
        return result;
    }

    unsigned u8()
    {
        return static_cast<unsigned>(number(1));
    }

    unsigned u16()
    {
        return static_cast<unsigned>(number(2));
    }

    std::uint32_t u32()
    {
        return static_cast<std::uint32_t>(number(4));
    }

    std::string raw(std::size_t length)
    {
        CHECK(length <= bytes.size() - offset);
        auto s = std::string(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
        return s;
    }

    std::string string()
    {
        return raw(u32());
    }

    Reader record(unsigned expected)
    {
        CHECK(u8() == expected && u8() == 1 && u16() == 0);
        const auto size = u32();
        CHECK(size <= bytes.size() - offset);
        Reader result{bytes.subspan(offset, size)};
        offset += size;
        return result;
    }

    void done()
    {
        CHECK(offset == bytes.size());
    }
};

template <class File>
Bytes collect(const File& file, std::size_t capacity, bool retry = false)
{
    std::vector<std::byte> output(capacity + 2, std::byte{0xa5}), again(capacity);
    resource::Cursor cursor = 0;
    Bytes all;
    for (std::size_t calls = 0; calls <= file.size() + 10; ++calls)
    {
        const auto result = file.read(cursor, {output.data() + 1, capacity});
        CHECK(result.status == resource::Status::Ok && result.written <= capacity);
        CHECK(output.front() == std::byte{0xa5} && output.back() == std::byte{0xa5});
        if (retry)
        {
            const auto repeated = file.read(cursor, again);
            CHECK(repeated.status == result.status && repeated.next == result.next &&
                  repeated.eof == result.eof && repeated.written == result.written &&
                  std::memcmp(again.data(), output.data() + 1, result.written) == 0);
        }
        all.insert(all.end(), output.begin() + 1, output.begin() + 1 + result.written);
        if (result.eof)
        {
            CHECK(all.size() == file.size());
            const auto eof = file.read(result.next, {});
            CHECK(eof.status == resource::Status::Ok && eof.written == 0 && eof.eof);
            return all;
        }
        CHECK(result.written != 0 && result.next != cursor);
        cursor = result.next;
    }
    CHECK(false);
    return {};
}

inline void save(const std::string& directory, const char* name, const Bytes& bytes)
{
    std::ofstream out(directory + "/" + name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    CHECK(out.good());
}
