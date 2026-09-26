// Shared host/MCU correctness checks; no heap, text formatting or test framework.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#pragma once
#include <telemetry/Telemetry.h>
#include <resource/telemetry/TelemetryFiles.hpp>
#include "../resources/Golden.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace review_embedded {
struct Result { unsigned checks = 0, failures = 0, firstLine = 0; };
inline Result result;
inline unsigned getterCalls = 0, commandCalls = 0;
inline float voltage() noexcept { ++getterCalls; return 1.f; }
inline telemetry::CommandResult configure(float) noexcept
{ ++commandCalls; return telemetry::CommandResult::Executed; }
inline constexpr telemetry::FieldTable fields{
    telemetry::field<&voltage>("V", "V", telemetry::limits(230.f, 0.f, 300.f))};
inline constexpr telemetry::FieldCatalogTable fieldGroups{telemetry::group("m", fields)};
inline constexpr telemetry::CommandTable commands{
    telemetry::command<&configure>("C", telemetry::arg<0>("P", "V", 230.f, 0.f, 300.f))};
inline constexpr telemetry::CommandCatalogTable commandGroups{telemetry::group("m", commands)};

inline void check(bool value, unsigned line) noexcept
{
    ++result.checks;
    if (!value) { ++result.failures; if (result.firstLine == 0) result.firstLine = line; }
}
#define REVIEW_CHECK(...) ::review_embedded::check(bool(__VA_ARGS__), __LINE__)

template <class T, class U>
TELEMETRY_NOINLINE bool convert(U value, T& target) noexcept
{ return telemetry::detail::convertNumberTo(value, target); }

inline void numeric() noexcept
{
    // Source values are volatile so ARM must execute the libgcc conversions,
    // including the boundaries that the host long-double oracle cannot cover.
    volatile double input = 0x1.fffffffffffffp63;
    std::uint64_t u = 7;
    REVIEW_CHECK(convert(input, u) && u == UINT64_MAX - UINT64_C(2047));
    input = 0x1p64;
    REVIEW_CHECK(!convert(input, u) && u == UINT64_MAX - UINT64_C(2047));
    input = -0.75;
    REVIEW_CHECK(convert(input, u) && u == 0);
    input = -1;
    REVIEW_CHECK(!convert(input, u) && u == 0);
    std::int64_t s = 7;
    input = 0x1p63;
    REVIEW_CHECK(!convert(input, s) && s == 7);
    input = -0x1p63;
    REVIEW_CHECK(convert(input, s) && s == INT64_MIN);
    input = 0x1.fffffffffffffp62;
    REVIEW_CHECK(convert(input, s) && s == INT64_MAX - INT64_C(1023));
    input = 4294967295.5;
    std::uint32_t u32 = 0;
    REVIEW_CHECK(convert(input, u32) && u32 == UINT32_MAX);
    input = 65535.9;
    std::uint16_t u16 = 0;
    REVIEW_CHECK(convert(input, u16) && u16 == UINT16_MAX);
    input = 65536;
    REVIEW_CHECK(!convert(input, u16) && u16 == UINT16_MAX);
    input = std::numeric_limits<double>::quiet_NaN();
    REVIEW_CHECK(!convert(input, u) && !convert(input, s));
    float f = 0;
    REVIEW_CHECK(convert(input, f) && std::isnan(f));
    input = std::numeric_limits<double>::infinity();
    REVIEW_CHECK(!convert(input, u) && !convert(input, s));
    REVIEW_CHECK(convert(input, f) && std::isinf(f));
    input = 1.e300;
    REVIEW_CHECK(!convert(input, f));
    volatile std::uint64_t integer = UINT64_MAX;
    double d = 0;
    REVIEW_CHECK(convert(integer, d) && d == 0x1p64);
    REVIEW_CHECK(convert(integer, f) && f == 0x1p64f);
    integer = 16777217;
    REVIEW_CHECK(convert(integer, f) && f == 16777216.f);
}

inline unsigned hexDigit(char value) noexcept
{ return static_cast<unsigned>(value <= '9' ? value - '0' : value - 'a' + 10); }

template <class File, std::size_t N>
inline void golden(const File& file, const char (&expected)[N], unsigned chunk) noexcept
{
    REVIEW_CHECK(file.size() == (N - 1) / 2);
    std::array<std::byte, 256> buffer{};
    resource::Cursor cursor = 0;
    std::size_t offset = 0;
    bool eof = false;
    for (unsigned attempt = 0; attempt < N && !eof; ++attempt) {
        const auto reply = file.read(cursor, resource::Output{buffer}.first(chunk));
        REVIEW_CHECK(reply.status == resource::Status::Ok && reply.written <= chunk);
        if (reply.status != resource::Status::Ok || reply.written > chunk) return;
        REVIEW_CHECK(reply.written != 0 || reply.eof);
        if (offset + reply.written > (N - 1) / 2) { REVIEW_CHECK(false); return; }
        for (unsigned i = 0; i < reply.written; ++i, ++offset) {
            const auto value = hexDigit(expected[2 * offset]) * 16 + hexDigit(expected[2 * offset + 1]);
            REVIEW_CHECK(std::to_integer<unsigned>(buffer[i]) == value);
        }
        cursor = reply.next;
        eof = reply.eof;
    }
    REVIEW_CHECK(eof && offset == file.size());
    const auto end = file.read(cursor, buffer);
    REVIEW_CHECK(end.status == resource::Status::Ok && end.eof && end.written == 0 && end.next == cursor);
}

inline Result run() noexcept
{
    result = {};
    getterCalls = commandCalls = 0;
    numeric();
    const auto index = fieldGroups.index();
    const auto commandIndex = commandGroups.index();
    volatile std::uint64_t tooWide = UINT64_C(1) << 32;
    REVIEW_CHECK(!telemetry::tryMakeId(0, tooWide));
    REVIEW_CHECK(index.find(tooWide) == nullptr);
    REVIEW_CHECK(index.read(tooWide).type() == telemetry::ScalarType::Null);
    REVIEW_CHECK(commandIndex.call(tooWide, 1) == telemetry::CommandResult::NotFound);
    REVIEW_CHECK(commands.call(tooWide, 1) == telemetry::CommandResult::NotFound);
    REVIEW_CHECK(getterCalls == 0 && commandCalls == 0);
    REVIEW_CHECK(commands.call<0>(1) == telemetry::CommandResult::Executed && commandCalls == 1);

    telemetry::DelegateSlot<void(int&) noexcept> owned;
    owned.bind([](auto& value) noexcept { value = 11; });
    int value = 0;
    owned.invoke(value);
    REVIEW_CHECK(value == 11);

    // Construct only immutable descriptions; none of this may sample a value.
    const telemetry_resource::SchemaFile schema{index};
    const telemetry_resource::CommandsFile commandFile{commandIndex};
    const telemetry_resource::ValuesFile values{schema};
    REVIEW_CHECK(getterCalls == 0);
    for (unsigned chunk : {1u, 2u, 3u, 7u, 31u, 256u}) {
        golden(schema, schemaGolden, chunk);
        golden(commandFile, commandsGolden, chunk);
    }
    REVIEW_CHECK(getterCalls == 0);
    for (unsigned chunk : {5u, 7u, 31u, 256u}) {
        const auto before = getterCalls;
        golden(values, valuesGolden, chunk);
        REVIEW_CHECK(getterCalls == before + 1);
    }
    return result;
}
#undef REVIEW_CHECK
} // namespace review_embedded
