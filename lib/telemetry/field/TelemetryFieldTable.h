/**
 * @file TelemetryFieldTable.h
 * @brief Position-identified fields with native compile-time read/write access.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_TABLE_H
#define TELEMETRY_FIELD_TABLE_H

#include "TelemetryFieldFactory.h"
#include <array>
#include <tuple>

namespace telemetry {
template <class... Definitions>
class FieldTable {
    static_assert((detail::IsFieldDefinition<Definitions>::value && ...),
                  "FieldTable accepts only field(...) definitions");
    static_assert(sizeof...(Definitions) <= idComponentCapacity,
                  "FieldTable exceeds the 16-bit entry capacity");
    using Types = std::tuple<Definitions...>;
    std::array<Field, sizeof...(Definitions)> fields_;
public:
    using Descriptor = Field;
    constexpr explicit FieldTable(Definitions... definitions) noexcept
        : fields_{definitions.materialize()...} {}
    // Views borrow this exact array address. Construct it directly in place.
    FieldTable(const FieldTable&) = delete;
    FieldTable(FieldTable&&) = delete;
    FieldTable& operator=(const FieldTable&) = delete;
    FieldTable& operator=(FieldTable&&) = delete;
    constexpr const Field* data() const & noexcept { return fields_.data(); }
    const Field* data() const && = delete;
    constexpr std::size_t size() const noexcept { return fields_.size(); }
    constexpr const Field& operator[](std::size_t i) const & noexcept { return fields_[i]; }
    const Field& operator[](std::size_t) const && = delete;

    template <std::size_t I>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto read() const noexcept
    {
        static_assert(I < sizeof...(Definitions), "Typed field index is outside FieldTable");
        if constexpr (I < sizeof...(Definitions))
            return std::tuple_element_t<I, Types>::read(fields_[I]);
    }
    template <std::size_t I, class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE std::optional<T> read() const noexcept
    {
        static_assert(I < sizeof...(Definitions), "Typed field index is outside FieldTable");
        if constexpr (I < sizeof...(Definitions))
            return std::tuple_element_t<I, Types>::template read<T>(fields_[I]);
        else return std::nullopt;
    }
    template <std::size_t I, class T,
              std::enable_if_t<detail::isScalarNumber<T> || std::is_same_v<T, Scalar>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE WriteResult write(T value) const noexcept
    {
        static_assert(I < sizeof...(Definitions), "Typed field index is outside FieldTable");
        if constexpr (I < sizeof...(Definitions))
            return std::tuple_element_t<I, Types>::write(fields_[I], value);
        else return WriteResult::NotFound;
    }
};
template <class... Definitions> FieldTable(Definitions...) -> FieldTable<std::decay_t<Definitions>...>;
} // namespace telemetry
#endif
