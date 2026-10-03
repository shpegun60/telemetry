/*
 * @file AbiMismatch.cpp
 * @brief New adapters must retain exact layout dependencies at link time.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/model/Adapter.hpp>
namespace ts = telemetry;
template <class> struct Changed;
template <std::uint64_t... Parts>
struct Changed<ts::detail::StructuredAbiTag<Parts...>> {
    inline static constexpr std::array values{Parts...};
    // Guard the selected dimensions themselves so tag maintenance cannot
    // silently turn the test into a mutation of an unrelated old member.
    static_assert(values[75] == sizeof(ts::FieldEntry));
    static_assert(values[79] == offsetof(ts::FieldEntry, write));
    static_assert(values[82] == offsetof(ts::FieldEntry, readScratchBytes));
    static_assert(values[83] == sizeof(ts::CommandEntry));
    static_assert(values[132] == offsetof(ts::FieldEntry, writeScratchBytes));
    static_assert(CASE >= 1 && CASE <= 5);
    static constexpr std::size_t selected = CASE == 1 ? 75 : CASE == 2 ? 79 :
                                            CASE == 3 ? 83 : CASE == 4 ? 82 : 132;
    template <std::size_t... I>
    static auto change(std::index_sequence<I...>)
        -> ts::detail::StructuredAbiTag<(values[I] + (I == selected ? 1 : 0))...>;
    using type = decltype(change(std::make_index_sequence<sizeof...(Parts)>{}));
};
using Wrong = Changed<ts::detail::CurrentStructuredAbiTag>::type;
namespace telemetry {
EncodedReadResult readFieldEncoded(ModelView, telemetry::PackedId, std::span<std::byte>, Workspace&, Wrong) noexcept;
EncodedWriteResult writeFieldEncoded(ModelView, telemetry::PackedId, std::span<const std::byte>, Workspace&, Wrong) noexcept;
EncodedCommandResult executeCommandEncoded(ModelView, telemetry::PackedId, std::span<const std::byte>, Workspace&, Wrong) noexcept;
}
int main()
{
    ts::Workspace workspace{std::span<std::byte>{}};
    ts::ModelView view{ts::TypeRegistryView{}, ts::ServiceIndex{nullptr, 0}, nullptr, 0};
#if CASE == 1 || CASE == 4
    return static_cast<int>(ts::readFieldEncoded(view, 0, {}, workspace, Wrong{}).dispatch);
#elif CASE == 2 || CASE == 5
    return static_cast<int>(ts::writeFieldEncoded(view, 0, {}, workspace, Wrong{}).dispatch);
#elif CASE == 3
    return static_cast<int>(ts::executeCommandEncoded(view, 0, {}, workspace, Wrong{}).dispatch);
#endif
}
