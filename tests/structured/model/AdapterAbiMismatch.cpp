/*
 * @file AdapterAbiMismatch.cpp
 * @brief A real compiled adapter must reject a differently built client.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/model/Adapter.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace ts = telemetry;

template <class Tag, std::size_t Index>
struct Mutate;

template <std::uint64_t... Parts, std::size_t Index>
struct Mutate<ts::detail::StructuredAbiTag<Parts...>, Index> {
    inline static constexpr std::array values{Parts...};
    static_assert(Index < sizeof...(Parts));

    template <std::size_t... I>
    static auto changed(std::index_sequence<I...>)
        -> ts::detail::StructuredAbiTag<(values[I] + (I == Index ? 1 : 0))...>;

    using type = decltype(changed(std::make_index_sequence<sizeof...(Parts)>{}));
};

#if CASE == 1
using Wrong = typename Mutate<ts::detail::CurrentStructuredAbiTag, 0>::type;
#elif CASE == 2
using Wrong = typename Mutate<ts::detail::CurrentStructuredAbiTag, 33>::type;
#elif CASE == 3
using Wrong = typename Mutate<ts::detail::CurrentStructuredAbiTag, 60>::type;
#else
#error Select a mismatched ABI dimension
#endif

namespace telemetry {
EncodedCallResult callServiceEncoded(ModelView, telemetry::PackedId,
                                     std::span<const std::byte>,
                                     std::span<std::byte>, Workspace&, Wrong) noexcept;
}

int main()
{
    ts::Workspace workspace{std::span<std::byte>{}};
    ts::ModelView view{ts::TypeRegistryView{}, ts::ServiceIndex{nullptr, 0}, nullptr, 0};
    auto result = ts::callServiceEncoded(view, 0, {}, {}, workspace, Wrong{});
    return static_cast<int>(result.dispatch);
}
