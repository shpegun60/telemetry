// Exhaustive bounded-string windows, atomic values and u32 limits (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <resource/telemetry/detail/Stream.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

unsigned checks = 0;
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

std::string escaped(std::string_view input)
{
    std::string expected = "\"";
    for (unsigned char value : input)
    {
        if (value < 32)
        {
            char escape[7];
            std::snprintf(escape, sizeof(escape), "\\u%04x", unsigned(value));
            expected += escape;
        }
        else
        {
            if (value == '"' || value == '\\')
            {
                expected += '\\';
            }
            expected += static_cast<char>(value);
        }
    }
    return expected + '"';
}

void window(std::string_view input, std::string_view expected, std::uint32_t skip,
            std::size_t capacity)
{
    std::array<std::byte, 66> output;
    output.fill(std::byte{0xa5});
    telemetry_resource::detail::Writer writer{{output.data() + 1, capacity}, skip};
    const bool complete = writer.string(input);
    const auto prefix = std::min<std::size_t>(skip, expected.size());
    const auto available = expected.size() - prefix;
    const auto used = std::min(capacity, available);
    CHECK(writer.ok() && complete == (available <= capacity));
    CHECK(writer.used() == used && writer.skip() == skip - prefix);
    CHECK(std::string_view(reinterpret_cast<const char*>(output.data() + 1), used) ==
          expected.substr(prefix, used));
    CHECK(output[0] == std::byte{0xa5} && output[1 + capacity] == std::byte{0xa5});
}

int main()
{
    using namespace telemetry_resource::detail;
    std::string bytes;
    for (unsigned value = 0; value < 256; ++value)
    {
        bytes += static_cast<char>(value);
    }
    for (const auto& input : {std::string{}, std::string{"a\"b\\c\nd"}, bytes})
    {
        const auto expected = escaped(input);
        Writer counter;
        CHECK(counter.string(input) && counter.count() == expected.size());
        for (std::uint32_t skip = 0; skip <= expected.size() + 1; ++skip)
        {
            for (std::size_t capacity = 0; capacity <= 32; ++capacity)
            {
                window(input, expected, skip, capacity);
            }
        }
    }
    std::string longText(8192, 'x');
    for (std::size_t at : {0u, 31u, 4096u, 8191u})
    {
        longText[at] = '\n';
    }
    const auto expected = escaped(longText);
    for (std::uint32_t skip : {0u, 1u, 31u, 4096u, 8192u, 8214u, 8215u})
    {
        for (std::size_t capacity : {0u, 1u, 2u, 3u, 7u, 31u, 64u})
        {
            window(longText, expected, skip, capacity);
        }
    }

    unsigned calls = 0;
    auto token = [&](resource::Output output) noexcept
    {
        ++calls;
        CHECK(output.size() == 3);
        output[0] = std::byte{'a'};
        output[1] = std::byte{'b'};
        output[2] = std::byte{'c'};
    };
    Stream measure;
    CHECK(measure.atomic(3, token) && measure.size() == 3 && measure.records() == 1 && calls == 0);
    std::array<std::byte, 5> output{};
    Stream small{0, resource::Output{output}.first(2)};
    CHECK(!small.atomic(3, token) && calls == 0);
    CHECK(small.result(1).status == resource::Status::BufferTooSmall);
    Stream invalid{pack(0, 1), output};
    CHECK(!invalid.atomic(3, token) && calls == 0);
    CHECK(invalid.result(1).status == resource::Status::InvalidCursor);
    Stream read{0, output};
    CHECK(read.atomic(3, token) && calls == 1);
    CHECK(read.result(1).next == pack(1, 0) && read.result(1).written == 3 && read.result(1).eof);
    CHECK(output[0] == std::byte{'a'} && output[2] == std::byte{'c'} && output[3] == std::byte{0});
    Stream skipped{pack(1, 0), output};
    CHECK(skipped.atomic(3, token) && calls == 1 && skipped.result(1).eof);

    // The u32 counters reject overflow before adding, without reserving or
    // accessing a huge buffer. Counting a live token never calls its emitter.
    Stream maximum;
    CHECK(maximum.atomic(std::numeric_limits<std::uint32_t>::max(), token));
    CHECK(maximum.size() == std::numeric_limits<std::uint32_t>::max());
    CHECK(!maximum.atomic(1, token) && !maximum.ok() && calls == 1);
    Writer maximumText;
    CHECK(maximumText.integer(std::numeric_limits<std::uint64_t>::max()));
    CHECK(maximumText.count() == 20);
    std::printf("Resource stream windows: %u checks passed\n", checks);
}
