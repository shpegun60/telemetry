/*
 * @file CodecProbe.cpp
 * @brief Independent byte, validation and object-lifetime checks for Stage 04.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>

using namespace telemetry;

namespace {

void require(bool condition)
{
    if (!condition) std::abort();
}

template <class T, std::size_t N = wireSize<T>>
void golden(const T& source, const std::array<std::byte, N>& expected)
{
    static_assert(wireSize<T> == N);
    std::array<std::byte, N> encoded{};
    require(encode(source, encoded) == CodecStatus::Ok);
    require(encoded == expected);

    alignas(T) std::array<std::byte, sizeof(T)> storage{};
    Workspace workspace{storage};
    auto lease = workspace.reserve<T>();
    T* decoded = nullptr;
    require(decode<T>(expected, lease, decoded) == CodecStatus::Ok);
    require(decoded != nullptr);
    std::array<std::byte, N> repeated{};
    require(encode(*decoded, repeated) == CodecStatus::Ok);
    require(repeated == expected);
}

enum class Mode : std::uint8_t { Off = 0, On = 1 };

struct State {
    float voltage;
    std::uint16_t rpm;
    bool enabled;
};

int dmiCalls = 0;
[[maybe_unused]] std::uint16_t dmiValue() noexcept
{
    ++dmiCalls;
    return 999;
}

struct Defaulted {
    std::uint16_t code = dmiValue();
    bool enabled = true;
};

struct Nested {
    std::array<State, 2> states;
    Mode mode;
};

struct Empty {};

void checkGoldens()
{
    golden<std::uint8_t>(255, {std::byte{0xff}});
    golden<std::int8_t>(-128, {std::byte{0x80}});
    golden<std::uint16_t>(0x1234, {std::byte{0x34}, std::byte{0x12}});
    golden<std::uint16_t>(0xffff, {std::byte{0xff}, std::byte{0xff}});
    golden<std::int16_t>(-32768, {std::byte{0x00}, std::byte{0x80}});
    golden<std::uint32_t>(0x89abcdefu,
        {std::byte{0xef}, std::byte{0xcd}, std::byte{0xab}, std::byte{0x89}});
    golden<std::uint32_t>(UINT32_MAX,
        {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}});
    golden<std::int32_t>(std::numeric_limits<std::int32_t>::min(),
        {std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}});
    golden<std::uint64_t>(UINT64_C(0x0123456789abcdef),
        {std::byte{0xef}, std::byte{0xcd}, std::byte{0xab}, std::byte{0x89},
         std::byte{0x67}, std::byte{0x45}, std::byte{0x23}, std::byte{0x01}});
    golden<std::uint64_t>(UINT64_MAX,
        {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
         std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}});
    golden<std::int64_t>(std::numeric_limits<std::int64_t>::min(),
        {std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
         std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}});
    golden<float>(std::bit_cast<float>(UINT32_C(0x80000000)),
        {std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80}});
    golden<float>(std::bit_cast<float>(UINT32_C(0x7f800001)),
        {std::byte{0x01}, std::byte{0x00}, std::byte{0x80}, std::byte{0x7f}});
    golden<double>(std::bit_cast<double>(UINT64_C(0x7ff8000000001234)),
        {std::byte{0x34}, std::byte{0x12}, std::byte{0x00}, std::byte{0x00},
         std::byte{0x00}, std::byte{0x00}, std::byte{0xf8}, std::byte{0x7f}});
    golden<bool>(true, {std::byte{0x01}});
    golden<Mode>(static_cast<Mode>(7), {std::byte{0x07}});
    golden<State>({1.0f, 0x1234, true},
        {std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3f},
         std::byte{0x34}, std::byte{0x12}, std::byte{0x01}});
    golden<Nested>(Nested{{State{1.0f, 0x1234, true}, State{-2.0f, 3, false}}, Mode::On},
        {std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3f},
         std::byte{0x34}, std::byte{0x12}, std::byte{0x01},
         std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xc0},
         std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}});

    golden<Defaulted>({42, false},
                      {std::byte{0x2a}, std::byte{0x00}, std::byte{0x00}});
    golden<std::array<Defaulted, 2>>({Defaulted{1, true}, Defaulted{2, false}},
        {std::byte{0x01}, std::byte{0x00}, std::byte{0x01},
         std::byte{0x02}, std::byte{0x00}, std::byte{0x00}});
    require(dmiCalls == 0);
    golden<std::array<std::uint16_t, 0>>({}, {});
    golden<Empty>({}, {});
    golden<std::array<Empty, 3>>({Empty{}, Empty{}, Empty{}}, {});
    golden<std::array<std::uint16_t, 3>>({0, 1, 0xffff},
        {std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
         std::byte{0x00}, std::byte{0xff}, std::byte{0xff}});
}

void checkFailures()
{
    constexpr std::array<std::byte, 7> expected{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3f},
        std::byte{0x34}, std::byte{0x12}, std::byte{0x01}};
    alignas(State) std::array<std::byte, sizeof(State)> storage{};
    Workspace workspace{storage};
    State sentinel{};

    for (std::size_t length = 0; length < expected.size(); ++length) {
        auto lease = workspace.reserve<State>();
        State* decoded = &sentinel;
        require(decode<State>(std::span{expected}.first(length), lease, decoded) ==
                CodecStatus::LengthMismatch);
        require(decoded == nullptr && !lease.constructed());
    }
    {
        std::array<std::byte, 8> extra{};
        auto lease = workspace.reserve<State>();
        State* decoded = nullptr;
        require(decode<State>(extra, lease, decoded) == CodecStatus::LengthMismatch);
        require(!lease.constructed());
    }
    for (const auto invalid : {std::byte{2}, std::byte{255}}) {
        auto bytes = expected;
        bytes.back() = invalid;
        auto lease = workspace.reserve<State>();
        State* decoded = nullptr;
        int callbacks = 0;
        if (decode<State>(bytes, lease, decoded) == CodecStatus::Ok) ++callbacks;
        require(callbacks == 0 && decoded == nullptr && !lease.constructed());
    }

    std::array<std::byte, 6> shortOutput{};
    State source{1.0f, 0x1234, true};
    require(encode(source, shortOutput) == CodecStatus::LengthMismatch);

    // The output points into the source object's own representation.
    auto selfBytes = std::as_writable_bytes(std::span{&source, 1});
    require(encode(source, selfBytes.first(wireSize<State>)) == CodecStatus::Overlap);

    auto lease = workspace.reserve<State>();
    State* decoded = nullptr;
    require(decode<State>(std::span<const std::byte>{storage.data(), 7},
                          lease, decoded) == CodecStatus::Overlap);
    require(!lease.constructed());

    std::array<std::byte, 12> related{};
    require(!buffersDisjoint(std::span{related}.first(4),
                             std::span{related}.subspan(3, 4), {}));
    require(buffersDisjoint(std::span{related}.first(4),
                            std::span{related}.subspan(4, 4),
                            std::span{related}.subspan(8, 4)));
    require(!buffersDisjoint({}, std::span{related}.subspan(3, 4),
                             std::span{related}.subspan(5, 4)));
}

void checkWorkspace()
{
    alignas(State) std::array<std::byte, scratchBytes<State> + 2> bytes{};
    constexpr std::array<std::byte, 7> input{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3f},
        std::byte{0x34}, std::byte{0x12}, std::byte{0x01}};
    {
        Workspace workspace{std::span{bytes}.subspan(1, scratchBytes<State>)};
        auto lease = workspace.reserve<State>();
        require(lease.valid());
        State* output = nullptr;
        require(decode<State>(input, lease, output) == CodecStatus::Ok);
        require(output->rpm == 0x1234);
        require(reinterpret_cast<std::uintptr_t>(output) % alignof(State) == 0);
        require(workspace.used() <= scratchBytes<State>);
    }
    {
        Workspace workspace{std::span{bytes}.subspan(1, sizeof(State))};
        auto lease = workspace.reserve<State>();
        require(!lease.valid());
        State* output = nullptr;
        require(decode<State>(input, lease, output) == CodecStatus::WorkspaceTooSmall);
    }
    {
        alignas(State) std::array<std::byte, sizeof(State)> exact{};
        Workspace workspace{exact};
        {
            auto lease = workspace.reserve<State>();
            State* output = nullptr;
            require(decode<State>(input, lease, output) == CodecStatus::Ok);
            require(workspace.used() == sizeof(State));
        }
        require(workspace.used() == 0);
    }
    {
        alignas(std::uint64_t)
            std::array<std::byte, scratchBytes<State> + scratchBytes<std::uint64_t>> both{};
        constexpr std::array<std::byte, 8> second{
            std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05},
            std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01}};
        Workspace workspace{both};
        {
            auto firstLease = workspace.reserve<State>();
            State* first = nullptr;
            require(decode<State>(input, firstLease, first) == CodecStatus::Ok);
            const auto firstUsage = workspace.used();
            {
                auto secondLease = workspace.reserve<std::uint64_t>();
                std::uint64_t* value = nullptr;
                require(decode<std::uint64_t>(second, secondLease, value) == CodecStatus::Ok);
                require(*value == UINT64_C(0x0102030405060708));
                require(workspace.used() > firstUsage);
            }
            require(workspace.used() == firstUsage && first->rpm == 0x1234);
        }
        require(workspace.used() == 0);
    }
}

} // namespace

int main()
{
    checkGoldens();
    checkFailures();
    checkWorkspace();
}
