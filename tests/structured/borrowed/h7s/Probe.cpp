/* Opaque callbacks and complete payload checks outside measurements.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "Fixture.hpp"
#include <memory>

namespace borrowed_h7s {
alignas(32) std::array<Small, 2> cache4{};
alignas(32) std::array<Large, 2> cache64{};
alignas(32) std::array<std::byte, 65536> output{};
alignas(32) std::array<std::byte, scratchCapacity> scratch{};
ts::Workspace workspace{scratch};
volatile std::uint32_t callbacks = 0;
namespace {
volatile std::uint32_t selected = 0;
std::uint32_t hashes[2][2];
void called() noexcept { callbacks = callbacks + 1; }
template <unsigned N> const Blob<N>& cached(unsigned id) noexcept
{
    if constexpr (N == 4096) return cache4[id & 1];
    else return cache64[id & 1];
}
template <unsigned N> __attribute__((noinline)) Blob<N> ownGet() noexcept
{ called(); return cached<N>(selected); }
template <unsigned N> __attribute__((noinline)) const Blob<N>& borrowGet() noexcept
{ called(); return cached<N>(selected); }
template <unsigned N> __attribute__((noinline)) ts::ServiceResult<Blob<N>> ownCall(const Request& q) noexcept
{
    called();
    if (q.selection & 2u) return ts::ServiceResult<Blob<N>>::failure(ts::ServiceStatus::Busy);
    return ts::ServiceResult<Blob<N>>::successFrom([&]() -> Blob<N> { return cached<N>(q.selection); });
}
template <unsigned N> __attribute__((noinline)) const Blob<N>& borrowCall(const Request& q) noexcept
{ called(); return cached<N>(q.selection); }
template <unsigned N> __attribute__((noinline)) ts::BorrowedServiceResult<Blob<N>> statusCall(const Request& q) noexcept
{
    called();
    if (q.selection & 2u) return ts::BorrowedServiceResult<Blob<N>>::failure(ts::ServiceStatus::Busy);
    return ts::BorrowedServiceResult<Blob<N>>::success(cached<N>(q.selection));
}
template <unsigned N> inline constexpr ts::FieldTable fields{
    ts::field<&ownGet<N>>("own"), ts::field<&borrowGet<N>>("borrow")};
template <unsigned N> inline constexpr ts::ServiceTable services{
    ts::service<&ownCall<N>>("own"), ts::service<&borrowCall<N>>("borrow")};
__attribute__((noinline)) std::uint32_t consume(const std::uint8_t* bytes, unsigned size) noexcept
{
    // The same full-payload FNV consumer is included in every measured path.
    std::uint32_t value = 2166136261u;
    for (unsigned i = 0; i < size; ++i) value = (value ^ bytes[i]) * 16777619u;
    return value;
}
template <unsigned N> std::span<std::byte> bytes() noexcept { return std::span{output}.first(N); }
template <unsigned N> __attribute__((noinline)) std::uint32_t nativeOwnField(std::uint32_t id) noexcept
{
    static_assert(N == 4096, "A 64 KiB owning native result does not fit the measurement stack");
    selected = id;
    const auto value = fields<N>.template read<0>();
    return value ? consume(value->bytes.data(), N) : 0;
}
template <unsigned N> __attribute__((noinline)) std::uint32_t nativeBorrowField(std::uint32_t id) noexcept
{
    selected = id;
    const auto value = fields<N>.template read<1>();
    return value.valueOrNull() == std::addressof(cached<N>(id)) ? consume(value->bytes.data(), N) : 0;
}
template <unsigned N, unsigned Position> __attribute__((noinline)) std::uint32_t encodedField(std::uint32_t id) noexcept
{
    selected = id;
    const auto result = fields<N>.data()[Position].readEncoded(bytes<N>(), workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.written == N && workspace.used() == 0
        ? consume(reinterpret_cast<const std::uint8_t*>(output.data()), N) : 0;
}
template <unsigned N> __attribute__((noinline)) std::uint32_t nativeOwnService(std::uint32_t id) noexcept
{
    static_assert(N == 4096, "A 64 KiB owning native result does not fit the measurement stack");
    const auto result = services<N>.template call<0>(Request{id});
    return result.hasValue() ? consume(result.value().bytes.data(), N) : 0;
}
template <unsigned N> __attribute__((noinline)) std::uint32_t nativeBorrowService(std::uint32_t id) noexcept
{
    const auto result = services<N>.template call<1>(Request{id});
    return result.valueOrNull() == std::addressof(cached<N>(id)) ? consume(result.value().bytes.data(), N) : 0;
}
template <unsigned N, unsigned Position> __attribute__((noinline)) std::uint32_t encodedService(std::uint32_t id) noexcept
{
    std::array<std::byte, 4> input{};
    (void)ts::encode(Request{id}, input);
    const auto result = services<N>.data()[Position].callEncoded(input, bytes<N>(), workspace);
    return result.dispatch == ts::DispatchStatus::Ok && result.endpointStatus == ts::ServiceStatus::Ok &&
        result.written == N && workspace.used() == 0 ? consume(reinterpret_cast<const std::uint8_t*>(output.data()), N) : 0;
}
template <unsigned N> void identities(Checks& result) noexcept
{
    const auto check = [&](bool value) { ++result.checked; if (!value) ++result.failed; };
    for (unsigned id = 0; id < 2; ++id) {
        selected = id;
        check(fields<N>.template read<1>().valueOrNull() == std::addressof(cached<N>(id)));
        check(services<N>.template call<1>(Request{id}).valueOrNull() == std::addressof(cached<N>(id)));
        if constexpr (N == 4096) {
            const auto ownField = fields<N>.template read<0>();
            check(ownField && std::addressof(*ownField) != std::addressof(cached<N>(id)));
            const auto ownService = services<N>.template call<0>(Request{id});
            check(ownService.hasValue() && ownService.valueOrNull() != std::addressof(cached<N>(id)));
        }
    }
    check(fields<N>.data()[0].readScratchBytes == ts::scratchBytes<Blob<N>> &&
          fields<N>.data()[0].writeScratchBytes == 0);
    check(fields<N>.data()[1].readScratchBytes == 0 && fields<N>.data()[1].writeScratchBytes == 0);
    check(services<N>.data()[0].scratchBytes >= sizeof(ts::ServiceResult<Blob<N>>));
    check(services<N>.data()[1].scratchBytes == 0);
    std::array<std::byte, 4> input{};
    (void)ts::encode(Request{0}, input);
    ts::FunctionSlot<const Blob<N>&() noexcept> fieldSlot;
    ts::FieldTable missingFields{ts::field("missing", fieldSlot)};
    callbacks = 0;
    const auto before = consume(reinterpret_cast<const std::uint8_t*>(output.data()), N);
    check(!missingFields.template read<0>());
    const auto missingRead = missingFields.data()[0].readEncoded(bytes<N>(), workspace);
    check(missingRead.dispatch == ts::DispatchStatus::Unavailable && missingRead.written == 0);
    check(callbacks == 0 && workspace.used() == 0 &&
          before == consume(reinterpret_cast<const std::uint8_t*>(output.data()), N));
    ts::FunctionSlot<const Blob<N>&(const Request&) noexcept> serviceSlot;
    ts::ServiceTable missingServices{ts::service("missing", serviceSlot)};
    const auto absent = missingServices.template call<0>(Request{0});
    check(absent.status() == ts::ServiceStatus::Unavailable && absent.valueOrNull() == nullptr);
    const auto missingCall = missingServices.data()[0].callEncoded(input, bytes<N>(), workspace);
    check(missingCall.dispatch == ts::DispatchStatus::Unavailable && missingCall.written == 0);
    check(callbacks == 0 && workspace.used() == 0 &&
          before == consume(reinterpret_cast<const std::uint8_t*>(output.data()), N));
    callbacks = 0;
    (void)ts::encode(Request{2}, input);
    const auto failure = services<N>.data()[0].callEncoded(input, bytes<N>(), workspace);
    check(failure.dispatch == ts::DispatchStatus::Ok && failure.endpointStatus == ts::ServiceStatus::Busy && failure.written == 0);
    check(callbacks == 1);
    check(workspace.used() == 0 && before == consume(reinterpret_cast<const std::uint8_t*>(output.data()), N));
}
struct Tracked {
    static inline unsigned live = 0, destroyed = 0;
    std::array<std::uint8_t, 4096> value{};
    Tracked() noexcept { ++live; }
    ~Tracked() noexcept { --live; ++destroyed; }
};
template <unsigned N> void statusWrapper(Checks& result) noexcept
{
    const auto check = [&](bool value) { ++result.checked; if (!value) ++result.failed; };
    constexpr ts::ServiceTable table{ts::service<&statusCall<N>>("status")};
    callbacks = 0;
    const auto value = table.template call<0>(Request{0});
    check(value.status() == ts::ServiceStatus::Ok && value.valueOrNull() == std::addressof(cached<N>(0)) && callbacks == 1);
    callbacks = 0;
    const auto failure = table.template call<0>(Request{2});
    check(failure.status() == ts::ServiceStatus::Busy && failure.valueOrNull() == nullptr && callbacks == 1);
    std::array<std::byte, 4> input{};
    (void)ts::encode(Request{0}, input);
    callbacks = 0;
    const auto encoded = table.data()[0].callEncoded(input, bytes<N>(), workspace);
    bool equal = true;
    for (unsigned i = 0; i < N; ++i) equal = (output[i] == std::byte{pattern(i, 0)}) && equal;
    result.payloadBytes += N;
    check(encoded.dispatch == ts::DispatchStatus::Ok && encoded.endpointStatus == ts::ServiceStatus::Ok &&
          encoded.written == N && equal && callbacks == 1 && workspace.used() == 0 && table.data()[0].scratchBytes == 0);
    const auto before = consume(reinterpret_cast<const std::uint8_t*>(output.data()), N);
    (void)ts::encode(Request{2}, input);
    callbacks = 0;
    const auto failed = table.data()[0].callEncoded(input, bytes<N>(), workspace);
    check(failed.dispatch == ts::DispatchStatus::Ok && failed.endpointStatus == ts::ServiceStatus::Busy &&
          failed.written == 0 && callbacks == 1 && workspace.used() == 0 &&
          before == consume(reinterpret_cast<const std::uint8_t*>(output.data()), N));
}
}

const std::array<Operation, operationCount> operations{{
    {"field_own_native_4k", nativeOwnField<4096>, 4096, 64, false},
    {"field_borrow_native_4k", nativeBorrowField<4096>, 4096, 64, false},
    {"field_own_encoded_4k", encodedField<4096, 0>, 4096, 64, true},
    {"field_borrow_encoded_4k", encodedField<4096, 1>, 4096, 64, true},
    {"service_own_native_4k", nativeOwnService<4096>, 4096, 64, false},
    {"service_borrow_native_4k", nativeBorrowService<4096>, 4096, 64, false},
    {"service_own_encoded_4k", encodedService<4096, 0>, 4096, 64, true},
    {"service_borrow_encoded_4k", encodedService<4096, 1>, 4096, 64, true},
    {"field_borrow_native_64k", nativeBorrowField<65536>, 65536, 8, false},
    {"field_own_encoded_64k", encodedField<65536, 0>, 65536, 8, true},
    {"field_borrow_encoded_64k", encodedField<65536, 1>, 65536, 8, true},
    {"service_borrow_native_64k", nativeBorrowService<65536>, 65536, 8, false},
    {"service_own_encoded_64k", encodedService<65536, 0>, 65536, 8, true},
    {"service_borrow_encoded_64k", encodedService<65536, 1>, 65536, 8, true},
}};
std::uint8_t pattern(unsigned position, unsigned selection) noexcept
{ return static_cast<std::uint8_t>(position * 17u + (position >> 8) + selection * 29u + 3u); }
void prepare() noexcept
{
    for (unsigned id = 0; id < 2; ++id) {
        for (unsigned i = 0; i < 4096; ++i) cache4[id].bytes[i] = pattern(i, id);
        for (unsigned i = 0; i < 65536; ++i) cache64[id].bytes[i] = pattern(i, id);
        hashes[0][id] = consume(cache4[id].bytes.data(), 4096);
        hashes[1][id] = consume(cache64[id].bytes.data(), 65536);
    }
}
std::uint32_t expected(unsigned size, unsigned id) noexcept { return hashes[size == 65536][id & 1]; }
void sequence(unsigned profile, std::array<std::uint32_t, sequenceSize>& ids) noexcept
{
    for (unsigned i = 0; i < sequenceSize; ++i) ids[i] = profile == 0 ? 0 : i & 1;
    if (profile == 2) {
        std::uint32_t state = 0x12345678u;
        for (unsigned i = sequenceSize - 1; i > 0; --i) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const unsigned j = state % (i + 1);
            const auto value = ids[i]; ids[i] = ids[j]; ids[j] = value;
        }
    }
}
std::uint32_t expectedSum(unsigned size, unsigned count, const std::array<std::uint32_t, sequenceSize>& ids) noexcept
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) value += expected(size, ids[i & (sequenceSize - 1)]);
    return value;
}
Checks check() noexcept
{
    Checks result;
    const auto check = [&](bool value) { ++result.checked; if (!value) ++result.failed; };
    for (const auto& op : operations) for (unsigned id = 0; id < 2; ++id) {
        callbacks = 0;
        check(op.invoke(id) == expected(op.bytes, id));
        check(callbacks == 1 && workspace.used() == 0);
        if (op.encoded) {
            bool equal = true;
            for (unsigned i = 0; i < op.bytes; ++i)
                equal = (output[i] == std::byte{pattern(i, id)}) && equal;
            result.payloadBytes += op.bytes;
            check(equal);
        }
    }
    identities<4096>(result); identities<65536>(result);
    const auto failed = services<4096>.template call<0>(Request{2});
    check(failed.status() == ts::ServiceStatus::Busy && failed.valueOrNull() == nullptr);
    for (unsigned status = 1; status <= 4; ++status) {
        const auto value = ts::BorrowedServiceResult<Large>::failure(static_cast<ts::ServiceStatus>(status));
        check(value.status() == static_cast<ts::ServiceStatus>(status) && !value.hasValue() && value.valueOrNull() == nullptr);
    }
    Tracked::live = Tracked::destroyed = 0;
    {
        auto own = ts::ServiceResult<Tracked>::successFrom([]() -> Tracked { return {}; });
        check(Tracked::live == 1 && Tracked::destroyed == 0 && own.hasValue());
        const auto view = ts::BorrowedServiceResult<Tracked>::success(own.value());
        check(view.valueOrNull() == own.valueOrNull() && Tracked::live == 1);
        const auto none = ts::ServiceResult<Tracked>::failure(ts::ServiceStatus::Unavailable);
        check(none.valueOrNull() == nullptr && Tracked::live == 1);
    }
    check(Tracked::live == 0 && Tracked::destroyed == 1);
    statusWrapper<4096>(result); statusWrapper<65536>(result);
    return result;
}
}
