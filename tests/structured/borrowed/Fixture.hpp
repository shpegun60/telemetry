/* Common DTOs only; no generated data or device operations. MIT. */
#pragma once
#include <telemetry/Telemetry.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace borrowed_fixture {
namespace ts = ::telemetry;
using DS = ts::DispatchStatus;
using SS = ts::ServiceStatus;
using WR = ts::WriteResult;
struct Request { bool ready; std::uint8_t seed; };
struct Config {
    std::uint32_t value;
    bool enabled;
    friend bool operator==(const Config&, const Config&) = default;
};
enum class Mode : std::int16_t { Off = -1, On = 2 };
template <std::size_t N> struct Blob {
    std::array<std::uint8_t, N> bytes;
    friend bool operator==(const Blob&, const Blob&) = default;
};
template <std::size_t N> void fill(Blob<N>& value, unsigned seed) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        value.bytes[i] = static_cast<std::uint8_t>((i * 37u + seed) & 255u);
}
template <std::size_t N> bool pattern(std::span<const std::byte> wire, unsigned seed) noexcept
{
    if (wire.size() != N) return false;
    for (std::size_t i = 0; i < N; ++i)
        if (wire[i] != static_cast<std::byte>((i * 37u + seed) & 255u)) return false;
    return true;
}
template <class T> struct Owner {
    T value;
    mutable unsigned reads = 0;
    unsigned writes = 0;
    const T& get() const noexcept { ++reads; return value; }
    T own() const noexcept { ++reads; return value; }
    WR set(const T& next) noexcept { ++writes; value = next; return WR::Applied; }
};
struct Device {
    Config value{0x12345678u, true};
    mutable unsigned reads = 0, calls = 0;
    const Config& get() const noexcept { ++reads; return value; }
    const Config& call(const Request&) const noexcept { ++calls; return value; }
    const Config& noRequest() const noexcept { ++calls; return value; }
};
inline Device first{}, second{{0x87654321u, false}};
inline const Config& getFirst() noexcept { return first.get(); }
inline const Config& getSecond() noexcept { return second.get(); }
inline const Config& callFirst(const Request& request) noexcept { return first.call(request); }
inline const Config& callSecond(const Request& request) noexcept { return second.call(request); }
inline const Config& contextGet(void* context) noexcept { return static_cast<Device*>(context)->get(); }
inline const Config& contextCall(void* context, const Request& request) noexcept
{ return static_cast<Device*>(context)->call(request); }
inline bool configBytes(std::span<const std::byte> bytes, const Config& value) noexcept
{
    if (bytes.size() != 5) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (bytes[i] != static_cast<std::byte>((value.value >> (8u * i)) & 255u)) return false;
    return bytes[4] == (value.enabled ? std::byte{1} : std::byte{0});
}
}
