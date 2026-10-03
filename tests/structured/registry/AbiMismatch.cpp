/*
 * @file AbiMismatch.cpp
 * @brief Link controls for structured revision, size and offset mismatches.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/abi/StructuredAbi.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace ts = telemetry;

template <class Tag, std::size_t Index>
struct MutateAbiPart;

template <std::uint64_t... Parts, std::size_t Index>
struct MutateAbiPart<ts::detail::StructuredAbiTag<Parts...>, Index> {
    inline static constexpr std::array values{Parts...};
    static_assert(Index < sizeof...(Parts));

    template <std::size_t... I>
    static auto changed(std::index_sequence<I...>)
        -> ts::detail::StructuredAbiTag<(values[I] + (I == Index ? 1 : 0))...>;

    using type = decltype(changed(std::make_index_sequence<sizeof...(Parts)>{}));
};

using Current = ts::detail::CurrentStructuredAbiTag;
static_assert(MutateAbiPart<Current, 0>::values[0] == ts::structuredAbiRevision);
static_assert(MutateAbiPart<Current, 15>::values[15] == sizeof(ts::TypeDescriptor));
static_assert(MutateAbiPart<Current, 26>::values[26] ==
              offsetof(ts::TypeDescriptor, enumData));

#if CASE == 1
using Wrong = typename MutateAbiPart<Current, 0>::type;
#elif CASE == 2
using Wrong = typename MutateAbiPart<Current, 15>::type;
#elif CASE == 3
using Wrong = typename MutateAbiPart<Current, 26>::type;
#else
#error Select a mismatched ABI dimension
#endif

int main()
{
    ts::requireStructuredAbi<Wrong>();
}
