/* Explicit borrowed Service result callbacks preserve status without payload copies. MIT. */
#include "Fixture.hpp"
#include "Check.hpp"
#include <algorithm>
#include <concepts>

using namespace borrowed_fixture;
using borrowed_test::check;
namespace {
template <std::size_t N, class R> struct Responder {
    Blob<N> response{};
    SS status = SS::Ok;
    unsigned calls = 0;
    ts::BorrowedServiceResult<Blob<N>> call(const R&) noexcept
    { ++calls; return status == SS::Ok ? ts::BorrowedServiceResult<Blob<N>>::success(response)
                                      : ts::BorrowedServiceResult<Blob<N>>::failure(status); }
};
template <std::size_t N> struct Responder<N, void> {
    Blob<N> response{};
    SS status = SS::Ok;
    unsigned calls = 0;
    ts::BorrowedServiceResult<Blob<N>> call() noexcept
    { ++calls; return status == SS::Ok ? ts::BorrowedServiceResult<Blob<N>>::success(response)
                                      : ts::BorrowedServiceResult<Blob<N>>::failure(status); }
};
template <std::size_t N, class R> void responses()
{
    using OwnerType = Responder<N, R>;
    static OwnerType owner{};
    owner.status = SS::Ok; owner.calls = 0;
    fill(owner.response, 67);
    const auto definition = ts::service<&OwnerType::call>("Status", owner);
    static_assert(decltype(definition)::borrowsResponse);
    static_assert(std::same_as<typename decltype(definition)::Response, Blob<N>>);
    static_assert(std::same_as<typename decltype(definition)::Result, ts::BorrowedServiceResult<Blob<N>>>);
    ts::ServiceTable table{definition};
    constexpr std::size_t requestBytes = ts::wireSize<R>;
    constexpr std::size_t required = [] {
        if constexpr (std::is_void_v<R>) return std::size_t{0};
        else if constexpr (sizeof(R) <= ts::maxLocalObjectBytes) return std::size_t{0};
        else return ts::scratchBytes<R>;
    }();
    static std::array<std::byte, N + 4> output{};
    static std::array<std::byte, requestBytes + 1> input{};
    static std::array<std::byte, required + 32> scratch{};
    input.fill(std::byte{7});
    if constexpr (std::is_same_v<R, Request>) input[0] = std::byte{1};
    const auto request = std::span{input}.first(requestBytes);
    ts::Workspace workspace{std::span{scratch}.subspan(1)};
    ts::Workspace empty{std::span<std::byte>{}};
    auto native = [&] {
        if constexpr (std::is_void_v<R>) return definition.call();
        else { static R value{}; return definition.call(value); }
    };
    check(native().valueOrNull() == std::addressof(owner.response) && owner.calls == 1);
    check(table[0].scratchBytes == required && table[0].responseWireBytes == N);
    output.fill(std::byte{0xcd});
    auto answer = table[0].callEncoded(request, output, workspace);
    check(answer.dispatch == DS::Ok && answer.endpointStatus == SS::Ok && answer.written == N &&
          pattern<N>(std::span{output}.first(N), 67) && output[N] == std::byte{0xcd} &&
          owner.calls == 2 && workspace.used() == 0);
    auto before = owner.calls;
    output.fill(std::byte{0xcd});
    answer = table[0].callEncoded(request, std::span{output}.first(N - 1), workspace);
    check(answer.dispatch == DS::BufferTooSmall && answer.written == 0 && owner.calls == before &&
          std::all_of(output.begin(), output.end(), [](auto v) { return v == std::byte{0xcd}; }));
    answer = table[0].callEncoded(input, output, workspace);
    check(answer.dispatch == DS::InvalidPayload && answer.written == 0 && owner.calls == before && workspace.used() == 0);
    for (auto status : {SS::Busy, SS::InvalidArgument, SS::Unavailable, SS::Failed}) {
        owner.status = status;
        const auto result = native();
        check(!result && result.status() == status && result.valueOrNull() == nullptr);
        auto outer = workspace.reserve<std::uint32_t>();
        auto* marker = outer.constructFrom([]() noexcept { return std::uint32_t{0x2468ace0}; });
        const auto prior = workspace.used();
        before = owner.calls;
        answer = table[0].callEncoded(request, output, workspace);
        check(marker != nullptr && *marker == 0x2468ace0 && answer.dispatch == DS::Ok &&
              answer.endpointStatus == status && answer.written == 0 && owner.calls == before + 1 &&
              workspace.used() == prior && std::all_of(output.begin(), output.end(), [](auto v) { return v == std::byte{0xcd}; }));
    }
    check(workspace.used() == 0);
    owner.status = SS::Ok;
    before = owner.calls;
    answer = table[0].callEncoded(request, output, empty);
    check(answer.dispatch == (required == 0 ? DS::Ok : DS::WorkspaceTooSmall) &&
          owner.calls == before + (required == 0 ? 1u : 0u) && empty.used() == 0);
    before = owner.calls;
    auto aliased = std::as_writable_bytes(std::span<Blob<N>>{std::addressof(owner.response), 1});
    answer = table[0].callEncoded(request, aliased, workspace);
    check(answer.dispatch == DS::InvalidPayload && answer.written == 0 && owner.calls == before + 1 &&
          pattern<N>(aliased, 67) && workspace.used() == 0);
    // Sixteen independent conditions per response/request pair.
}

struct RequestBacked {
    unsigned calls = 0;
    ts::BorrowedServiceResult<Request> call(const Request& request) noexcept
    { ++calls; return ts::BorrowedServiceResult<Request>::success(request); }
};
void requestBacked()
{
    RequestBacked owner;
    const auto definition = ts::service<&RequestBacked::call>("Request", owner);
    Request native{true, 19};
    check(definition.call(native).valueOrNull() == std::addressof(native));
    ts::ServiceTable table{definition};
    const std::array<std::byte, 2> input{std::byte{1}, std::byte{19}};
    std::array<std::byte, 2> output{};
    std::array<std::byte, 32> scratch{};
    ts::Workspace workspace{std::span{scratch}.subspan(1)};
    const auto result = table[0].callEncoded(input, output, workspace);
    check(result.dispatch == DS::Ok && result.written == 2 && input == output && owner.calls == 2 && workspace.used() == 0);
    // Callback view remains valid through encoding, regardless of Request storage policy.
}

struct SlotOwner {
    Config value;
    unsigned calls = 0;
    ts::BorrowedServiceResult<Config> call(const Request&) noexcept
    { ++calls; return ts::BorrowedServiceResult<Config>::success(value); }
};
inline SlotOwner firstSlot{{0x12345678, true}}, secondSlot{{0x87654321, false}};
ts::BorrowedServiceResult<Config> firstFunction(const Request& q) noexcept { return firstSlot.call(q); }
ts::BorrowedServiceResult<Config> secondFunction(const Request& q) noexcept { return secondSlot.call(q); }
ts::BorrowedServiceResult<Config> contextFunction(void* owner, const Request& q) noexcept
{ return static_cast<SlotOwner*>(owner)->call(q); }
template <class Definition, class Bind, class Reset> void slots(Definition definition, Bind bind, Reset reset)
{
    ts::ServiceTable table{definition};
    const Request request{true, 7};
    const std::array<std::byte, 2> input{std::byte{1}, std::byte{7}};
    std::array<std::byte, 5> output{};
    std::array<std::byte, 32> scratch{};
    ts::Workspace workspace{scratch};
    auto pair = [&](const Config* expected) {
        const auto before = firstSlot.calls + secondSlot.calls;
        const auto native = definition.call(request);
        output.fill(std::byte{0xcd});
        const auto encoded = table[0].callEncoded(input, output, workspace);
        check((expected ? native.status() == SS::Ok && native.valueOrNull() == expected &&
                          encoded.dispatch == DS::Ok && encoded.endpointStatus == SS::Ok && encoded.written == 5 &&
                          configBytes(output, *expected) && firstSlot.calls + secondSlot.calls == before + 2
                        : native.status() == SS::Unavailable && native.valueOrNull() == nullptr &&
                          encoded.dispatch == DS::Unavailable && encoded.written == 0 && output[0] == std::byte{0xcd} &&
                          firstSlot.calls + secondSlot.calls == before) && workspace.used() == 0);
    };
    pair(nullptr); bind(false); pair(std::addressof(firstSlot.value));
    bind(true); pair(std::addressof(secondSlot.value)); reset(); pair(nullptr);
}
void slotMatrix()
{
    using Signature = ts::BorrowedServiceResult<Config>(const Request&) noexcept;
    ts::OwnerSlot<SlotOwner> owner;
    slots(ts::service<&SlotOwner::call>("Owner", owner), [&](bool alt) { owner.bind(alt ? secondSlot : firstSlot); }, [&] { owner.reset(); });
    ts::FunctionSlot<Signature> function;
    slots(ts::service("Function", function), [&](bool alt) { function.bind(alt ? &secondFunction : &firstFunction); }, [&] { function.reset(); });
    ts::ContextFunctionSlot<Signature> context;
    slots(ts::service("Context", context), [&](bool alt) { context.bind(&contextFunction, alt ? &secondSlot : &firstSlot); }, [&] { context.reset(); });
    auto first = [](const Request& q) noexcept { return firstSlot.call(q); };
    auto second = [](const Request& q) noexcept { return secondSlot.call(q); };
    ts::DelegateRefSlot<Signature> reference;
    slots(ts::service("Reference", reference), [&](bool alt) {
        if (alt) reference.bind(second); else reference.bind(first);
    }, [&] { reference.reset(); });
    ts::DelegateSlot<Signature> delegate;
    slots(ts::service("Delegate", delegate), [&](bool alt) {
        auto* selected = alt ? &secondSlot : &firstSlot;
        delegate.bind([selected](const Request& q) noexcept { return selected->call(q); });
    }, [&] { delegate.reset(); });
}
}

int main()
{
    borrowed_test::start();
    responses<4096, void>(); responses<4096, Request>(); responses<4096, Blob<16>>();
    responses<4096, Blob<32>>(); responses<4096, Blob<64>>(); responses<4096, Blob<4096>>();
    responses<65536, void>(); responses<65536, Request>(); responses<65536, Blob<16>>();
    responses<65536, Blob<32>>(); responses<65536, Blob<64>>(); responses<65536, Blob<4096>>();
    requestBacked(); slotMatrix();
    return borrowed_test::finish(); // 12*16 + 2 + 20 + 1 =215.
}
