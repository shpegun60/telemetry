/*
 * @file ErasedBoundary.cpp
 * @brief Checked entry boundaries, direct binding contexts and in-place Service I/O.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/model/Model.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ts = telemetry;
using DS = ts::DispatchStatus;
using WR = telemetry::WriteResult;
#define CHECK(...) do { if (!(__VA_ARGS__)) return __LINE__; } while (false)

namespace {

template <std::size_t N>
struct Payload {
    std::array<std::uint32_t, N / 4> words;
    bool enabled;
};

template <std::size_t In, std::size_t Out>
struct Transform {
    int calls = 0;
    ts::ServiceResult<Payload<Out>> call(const Payload<In>& request) noexcept
    {
        ++calls;
        if (!request.enabled)
            return ts::ServiceResult<Payload<Out>>::failure(ts::ServiceStatus::Busy);
        return ts::ServiceResult<Payload<Out>>::successFrom([&]() noexcept {
            Payload<Out> response{};
            for (std::size_t i = 0; i < response.words.size(); ++i)
                response.words[i] = request.words[i % request.words.size()] + 7;
            response.enabled = true;
            return response;
        });
    }
};

// Test all storage combinations at every configured budget. Aliased input is
// consumed entirely before encoding; unused bytes and error responses stay intact.
template <std::size_t In, std::size_t Out>
int serviceBuffers()
{
    Transform<In, Out> target;
    ts::ServiceTable table{ts::service<&Transform<In, Out>::call>("transform", target)};
    const auto& entry = table.data()[0];
    CHECK(entry.context == &target);
    std::array<std::byte, 512> scratch{};
    ts::Workspace workspace{scratch};
    Payload<In> request{};
    for (std::size_t i = 0; i < request.words.size(); ++i)
        request.words[i] = static_cast<std::uint32_t>(i + 11);
    request.enabled = true;
    std::array<std::byte, ts::wireSize<Payload<In>>> input{};
    std::array<std::byte, ts::wireSize<Payload<Out>>> expected{};
    CHECK(ts::encode(request, input) == ts::CodecStatus::Ok);
    CHECK(entry.callEncoded(input, expected, workspace).dispatch == DS::Ok);

    for (const auto offsets : {std::array<std::size_t, 2>{4, 4}, {3, 5}, {5, 3}}) {
        std::array<std::byte, 192> shared;
        shared.fill(std::byte{0xa5});
        std::copy(input.begin(), input.end(), shared.begin() + offsets[0]);
        const auto before = shared;
        const auto result = entry.callEncoded(std::span{shared}.subspan(offsets[0], input.size()),
            std::span{shared}.subspan(offsets[1], expected.size()), workspace);
        CHECK(result.dispatch == DS::Ok && result.endpointStatus == ts::ServiceStatus::Ok);
        CHECK(result.written == expected.size() && workspace.used() == 0);
        for (std::size_t i = 0; i < shared.size(); ++i) {
            const auto wanted = i >= offsets[1] && i < offsets[1] + expected.size()
                ? expected[i - offsets[1]] : before[i];
            CHECK(shared[i] == wanted);
        }
    }

    std::array<std::byte, 192> output;
    output.fill(std::byte{0xa5});
    const auto unchanged = output;
    const int calls = target.calls;
    CHECK(entry.callEncoded(std::span{input}.first(input.size() - 1), output, workspace).dispatch == DS::InvalidPayload);
    CHECK(entry.callEncoded(input, std::span{output}.first(expected.size() - 1), workspace).dispatch == DS::BufferTooSmall);
    auto invalid = input;
    invalid.back() = std::byte{2};
    CHECK(entry.callEncoded(invalid, output, workspace).dispatch == DS::InvalidPayload);
    CHECK(target.calls == calls && output == unchanged && workspace.used() == 0);

    // A valid application failure writes no response bytes, also in-place.
    std::copy(input.begin(), input.end(), output.begin());
    output[input.size() - 1] = std::byte{0};
    const auto beforeFailure = output;
    const auto failure = entry.callEncoded(std::span{output}.first(input.size()), output, workspace);
    CHECK(failure.dispatch == DS::Ok && failure.endpointStatus == ts::ServiceStatus::Busy);
    CHECK(failure.written == 0 && output == beforeFailure);

    const auto aliasStatus = entry.scratchBytes == 0 ? DS::Ok : DS::InvalidPayload;
    CHECK(entry.callEncoded(input, std::span{scratch}.first(expected.size()), workspace).dispatch == aliasStatus);
    std::copy(input.begin(), input.end(), scratch.begin());
    CHECK(entry.callEncoded(std::span{scratch}.first(input.size()), output, workspace).dispatch == aliasStatus);
    CHECK(workspace.used() == 0);
    return 0;
}

struct Device {
    std::uint32_t value = 17;
    std::uint32_t get() const noexcept { return value; }
    WR set(std::uint32_t next) noexcept { value = next; return WR::Applied; }
};
struct Prefix { std::uint64_t unused = 0; };
struct Derived : Prefix, Device {};

int contexts()
{
    Derived owner;
    ts::FieldTable methods{ts::field<&Device::get, &Device::set>("derived", owner)};
    const Derived fixed{};
    ts::FieldTable constants{ts::field<&Device::get>("constant", fixed)};
    CHECK(methods.data()[0].readContext == &owner && methods.data()[0].writeContext == &owner);
    CHECK(constants.data()[0].readContext == &fixed);
    std::array<std::byte, 64> scratch{};
    std::array<std::byte, 4> bytes{std::byte{42}};
    ts::Workspace workspace{scratch};
    CHECK(methods.data()[0].writeEncoded(bytes, workspace).endpointStatus == WR::Applied);
    CHECK(owner.value == 42);
    CHECK(constants.data()[0].readEncoded(bytes, workspace).dispatch == DS::Ok && bytes[0] == std::byte{17});
    CHECK(constants.data()[0].writeEncoded({}, workspace).endpointStatus == WR::ReadOnly);

    Device reader, writer;
    auto get = [&reader]() noexcept { return reader.get(); };
    auto set = [&writer](std::uint32_t value) noexcept { return writer.set(value); };
    ts::FieldTable separate{ts::field("separate", get, set)};
    const auto& entry = separate.data()[0];
    CHECK(entry.readContext == &get && entry.writeContext == &set);
    bytes[0] = std::byte{29};
    CHECK(entry.writeEncoded(bytes, workspace).endpointStatus == WR::Applied);
    CHECK(writer.value == 29 && reader.value == 17);
    CHECK(entry.readEncoded(bytes, workspace).dispatch == DS::Ok && bytes[0] == std::byte{17});
    CHECK(entry.writeEncoded(std::span{bytes}.first(3), workspace).dispatch == DS::InvalidPayload);
    CHECK(entry.readEncoded(std::span{bytes}.first(3), workspace).dispatch == DS::BufferTooSmall);
    CHECK(writer.value == 29);

    telemetry::OwnerSlot<Device> slot;
    ts::FieldTable delayed{ts::field<&Device::get, &Device::set>("slot", slot)};
    const auto& late = delayed.data()[0];
    CHECK(late.readContext == &slot && late.writeContext == &slot);
    CHECK(late.readEncoded(bytes, workspace).dispatch == DS::Unavailable);
    CHECK(late.writeEncoded(bytes, workspace).dispatch == DS::Unavailable);
    slot.bind(writer);
    CHECK(late.readEncoded(bytes, workspace).dispatch == DS::Ok && bytes[0] == std::byte{29});
    slot.bind(reader);
    bytes[0] = std::byte{37};
    CHECK(late.writeEncoded(bytes, workspace).endpointStatus == WR::Applied);
    CHECK(reader.value == 37 && writer.value == 29);
    slot.reset();
    CHECK(late.readEncoded(bytes, workspace).dispatch == DS::Unavailable);
    return 0;
}
} // namespace

int main()
{
    if (const auto result = contexts()) return result;
    if (const auto result = serviceBuffers<4, 4>()) return result;
    if (const auto result = serviceBuffers<4, 64>()) return result;
    if (const auto result = serviceBuffers<64, 4>()) return result;
    return serviceBuffers<64, 64>();
}
