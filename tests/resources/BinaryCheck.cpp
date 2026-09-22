// Golden scalar bytes and bounded writer controls (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include "TestSupport.hpp"
#include <resource/telemetry/detail/BinaryWriter.hpp>
using namespace telemetry_resource;
using detail::BinaryWriter;
using detail::OutputWriter;
using telemetry::Scalar;

void fingerprint()
{
    using detail::Fingerprint;
    // RFC 9923 section 8.3 known answers, including high-bit input bytes.
    constexpr std::string_view inputs[]{"", "a", "foobar", "Hello!\x01\xff\xed"};
    constexpr std::uint64_t expected[]{UINT64_C(0xcbf29ce484222325), UINT64_C(0xaf63dc4c8601ec8c),
                                       UINT64_C(0x85944171f73967e8), UINT64_C(0xbd51ea7094ee6fa1)};
    for (unsigned i = 0; i < std::size(inputs); ++i)
    {
        const auto bytes = std::as_bytes(std::span{inputs[i].data(), inputs[i].size()});
        for (std::size_t split = 0; split <= bytes.size(); ++split)
        {
            Fingerprint hash;
            hash.bytes(bytes.first(split));
            hash.bytes({});
            hash.bytes(bytes.subspan(split));
            CHECK(hash.value() == expected[i]);
            CHECK(Fingerprint{hash.value()}.value() == expected[i]);
        }
    }

    // Independent two-limb arithmetic checks carries and wrapping at every
    // prefix, then verifies that chunk boundaries and embedded NULs do not matter.
    std::array<std::byte, 4096> bytes{};
    std::uint32_t state = 0x63ab1739, low = 0x84222325, high = 0xcbf29ce4;
    Fingerprint incremental;
    for (auto& byte : bytes)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = std::byte(state & 255u);
        low ^= std::to_integer<std::uint8_t>(byte);
        const auto product = std::uint64_t{low} * 435u;
        high = high * 435u + (low << 8) + static_cast<std::uint32_t>(product >> 32);
        low = static_cast<std::uint32_t>(product);
        incremental.bytes({&byte, 1});
        CHECK(incremental.value() == ((std::uint64_t{high} << 32) | low));
    }
    for (const auto chunk : {1u, 2u, 3u, 7u, 31u, 256u, 4096u})
    {
        Fingerprint hash;
        for (std::size_t offset = 0; offset < bytes.size(); offset += chunk)
        {
            hash.bytes(std::span{bytes}.subspan(
                offset, std::min<std::size_t>(chunk, bytes.size() - offset)));
        }
        CHECK(hash.value() == incremental.value());
    }
}

void scalar(Scalar value, std::string_view hex)
{
    const auto expected = unhex(hex);
    BinaryWriter measure;
    CHECK(measure.scalar(value) && measure.count() == expected.size());
    for (std::size_t offset = 0; offset <= expected.size(); ++offset)
    {
        for (std::size_t size = 0; size <= expected.size() + 1; ++size)
        {
            std::array<std::byte, 32> buffer;
            buffer.fill(std::byte{0xa5});
            OutputWriter out{{buffer.data() + 1, size}, static_cast<std::uint32_t>(offset)};
            const bool done = out.scalar(value);
            const auto count = std::min(size, expected.size() - offset);
            CHECK(out.ok() && size - out.remaining() == count && out.skip() == 0);
            CHECK(done == (size >= expected.size() - offset));
            CHECK(std::equal(buffer.begin() + 1, buffer.begin() + 1 + count,
                             expected.begin() + offset));
            CHECK(buffer.front() == std::byte{0xa5} && buffer[size + 1] == std::byte{0xa5});
        }
    }
}

int main()
{
    fingerprint();
    scalar(Scalar{}, "00 00 00");
    scalar(Scalar::fromBool(false), "01 01 01 00");
    scalar(Scalar::fromBool(true), "01 01 01 01");
    scalar(Scalar::fromU8(255), "02 01 01 ff");
    scalar(Scalar::fromU16(0x1234), "03 01 02 34 12");
    scalar(Scalar::fromU32(0x12345678), "04 01 04 78 56 34 12");
    scalar(Scalar::fromU64(UINT64_MAX), "05 01 08 ff ff ff ff ff ff ff ff");
    scalar(Scalar::fromS8(-128), "06 01 01 80");
    scalar(Scalar::fromS16(-32768), "07 01 02 00 80");
    scalar(Scalar::fromS32(INT32_MIN), "08 01 04 00 00 00 80");
    scalar(Scalar::fromS64(INT64_MIN), "09 01 08 00 00 00 00 00 00 00 80");
    scalar(Scalar::fromF32(250), "0a 01 04 00 00 7a 43");
    scalar(Scalar::fromF32(-0.0f), "0a 01 04 00 00 00 80");
    scalar(Scalar::fromF32(std::numeric_limits<float>::max()), "0a 01 04 ff ff 7f 7f");
    scalar(Scalar::fromF32(std::bit_cast<float>(1u)), "0a 01 04 01 00 00 00");
    scalar(Scalar::fromF32(std::bit_cast<float>(0x7fc01234u)), "0a 01 04 34 12 c0 7f");
    scalar(Scalar::fromF32(std::numeric_limits<float>::infinity()), "0a 01 04 00 00 80 7f");
    scalar(Scalar::fromF64(-0.0), "0b 01 08 00 00 00 00 00 00 00 80");
    scalar(Scalar::fromF64(std::numeric_limits<double>::max()), "0b 01 08 ff ff ff ff ff ff ef 7f");
    scalar(Scalar::fromF64(std::bit_cast<double>(std::uint64_t{1})),
           "0b 01 08 01 00 00 00 00 00 00 00");
    scalar(Scalar::fromF64(std::bit_cast<double>(UINT64_C(0x7ff812345678abcd))),
           "0b 01 08 cd ab 78 56 34 12 f8 7f");
    scalar(Scalar::fromF64(-std::numeric_limits<double>::infinity()),
           "0b 01 08 00 00 00 00 00 00 f0 ff");
    constexpr std::string_view text{"A\"B\nC\\D\0\xc3\xa9", 10};
    std::array<std::byte, 30> buffer{};
    OutputWriter out{buffer};
    CHECK(out.string(text));
    Reader r{{buffer.data(), buffer.size() - out.remaining()}};
    CHECK(r.string() == text);
    r.done();
    // 65536 valid 64-KiB spans exceed the u32 file limit; measuring never reads them.
    std::array<std::byte, 65536> block{};
    BinaryWriter count;
    for (unsigned i = 0; i < 65535; ++i)
    {
        CHECK(count.bytes(block));
    }
    CHECK(!count.bytes(block) && !count.ok());
    CHECK(!count.u8(1));

    // Exact random bit patterns, including nonfinite floats: no arithmetic or text conversion.
    std::uint64_t state = UINT64_C(0x1a34bcf34d02);
    for (unsigned i = 0; i < 20000; ++i)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const std::array<Scalar, 4> inputs{
            Scalar::fromU64(state), Scalar::fromS64(std::bit_cast<std::int64_t>(state)),
            Scalar::fromF32(std::bit_cast<float>(static_cast<std::uint32_t>(state))),
            Scalar::fromF64(std::bit_cast<double>(state))};
        for (unsigned n = 0; n < inputs.size(); ++n)
        {
            std::array<std::byte, 11> bytes{};
            OutputWriter w{bytes};
            CHECK(w.scalar(inputs[n]));
            Reader r{{bytes.data(), bytes.size() - w.remaining()}};
            constexpr unsigned types[]{5, 9, 10, 11};
            CHECK(r.u8() == types[n] && r.u8() == 1);
            const auto payload = r.u8();
            CHECK(payload == (n == 2 ? 4u : 8u));
            CHECK(r.number(payload) == (n == 2 ? static_cast<std::uint32_t>(state) : state));
            r.done();
        }
    }
    std::printf("Binary scalar/writer: %u checks\n", checks);
}
