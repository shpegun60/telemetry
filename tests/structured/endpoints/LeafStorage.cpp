/*
 * @file LeafStorage.cpp
 * @brief Scalar/enum codec and scratch requirements under every storage policy.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry_structured/model/Model.hpp>
#include <algorithm>
#include <bit>

namespace ts = telemetry::structured;
namespace leaf {
enum class Mode : std::uint16_t { Off, On };
template <class T> inline T current{};
template <class T> inline unsigned writes = 0;
template <class T> T get() noexcept { return current<T>; }
template <class T> telemetry::WriteResult set(T value) noexcept
{
    current<T> = value;
    ++writes<T>;
    return telemetry::WriteResult::Applied;
}
template <class T>
inline constexpr ts::FieldTable rows{ts::field<&get<T>, &set<T>>("leaf")};
template <class T>
inline constexpr ts::FieldCatalogTable catalogs{ts::group("test", rows<T>)};

template <class T>
bool check(T value)
{
    static_assert(rows<T>.data()[0].scratchBytes == (sizeof(T) <= ts::maxLocalObjectBytes ? 0 : ts::scratchBytes<T>));
    std::array<std::byte, ts::wireSize<T> + 2> input{}, output{};
    auto bytes = std::span{input}.subspan(1, ts::wireSize<T>);
    auto encoded = std::span{output}.subspan(1, ts::wireSize<T>);
    std::array<std::byte, ts::scratchBytes<T>> storage;
    ts::Workspace workspace{sizeof(T) <= ts::maxLocalObjectBytes
        ? std::span<std::byte>{} : std::span{storage}};
    if (ts::encode(value, bytes) != ts::CodecStatus::Ok) return false;
    auto index = catalogs<T>.index();
    auto written = index.writeEncoded(0, bytes, workspace);
    if (written.dispatch != ts::DispatchStatus::Ok ||
        written.endpointStatus != telemetry::WriteResult::Applied || writes<T> != 1) return false;
    auto read = index.readEncoded(0, encoded, workspace);
    if (read.dispatch != ts::DispatchStatus::Ok || read.written != ts::wireSize<T> ||
        !std::equal(bytes.begin(), bytes.end(), encoded.begin()) || workspace.used() != 0) return false;
    if (index.readEncoded(0, encoded.first(ts::wireSize<T> - 1), workspace).dispatch !=
        ts::DispatchStatus::BufferTooSmall) return false;
    if (index.writeEncoded(0, {}, workspace).dispatch != ts::DispatchStatus::InvalidPayload ||
        writes<T> != 1) return false;
    return true;
}

struct One { std::uint8_t value; };
One getOne() noexcept { return {1}; }
std::array<std::uint8_t, 1> getArray() noexcept { return {1}; }
inline constexpr ts::FieldTable composites{
    ts::field<&getOne>("one"), ts::field<&getArray>("array")};
inline constexpr ts::FieldCatalogTable compositeCatalogs{ts::group("test", composites)};
static_assert(composites.data()[0].scratchBytes == (ts::maxLocalObjectBytes ? 0 : ts::scratchBytes<One>));
static_assert(composites.data()[1].scratchBytes == composites.data()[0].scratchBytes);
}

int main()
{
    using namespace leaf;
    if (!check(true) || !check(std::uint8_t{255}) || !check(std::uint16_t{65535}) ||
        !check(std::uint32_t{UINT32_MAX}) || !check(std::uint64_t{UINT64_MAX}) ||
        !check(std::int8_t{INT8_MIN}) || !check(std::int16_t{INT16_MIN}) ||
        !check(std::int32_t{INT32_MIN}) || !check(std::int64_t{INT64_MIN}) ||
        !check(std::bit_cast<float>(std::uint32_t{0x7fc01234})) ||
        !check(std::bit_cast<double>(UINT64_C(0xfff8123400000001))) ||
        !check(static_cast<Mode>(65535))) return 1;
    std::array<std::byte, 1> scratch;
    ts::Workspace workspace{scratch};
    ts::Workspace none{std::span<std::byte>{}};
    std::array<std::byte, 1> bytes{};
    for (auto invalid : {2u, 255u}) {
        bytes[0] = std::byte(invalid);
        if (catalogs<bool>.index().writeEncoded(0, bytes, workspace).dispatch !=
            ts::DispatchStatus::InvalidPayload || writes<bool> != 1) return 2;
    }
    for (unsigned index = 0; index != 2; ++index) {
        if (compositeCatalogs.index().readEncoded(index, bytes, none).dispatch !=
            (ts::maxLocalObjectBytes ? ts::DispatchStatus::Ok : ts::DispatchStatus::WorkspaceTooSmall)) return 3;
    }
    return 0;
}
