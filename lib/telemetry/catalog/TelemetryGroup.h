/**
 * @file TelemetryGroup.h
 * @brief Named, borrowed lvalue tables for position-identified catalog groups.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_GROUP_H
#define TELEMETRY_GROUP_H
#include <memory>
#include <type_traits>
namespace telemetry {
template <class...> class FieldTable;
template <class...> class CommandTable;
namespace detail {
template <class> struct IsLocalTable : std::false_type {};
template <class... D> struct IsLocalTable<FieldTable<D...>> : std::true_type {};
template <class... D> struct IsLocalTable<CommandTable<D...>> : std::true_type {};
template <class Table> struct TableGroup {
    using TableType = Table;
    const char* const name;
    const Table* const table;
    constexpr TableGroup(const char* label, const Table& value) noexcept
        : name(label), table(std::addressof(value)) {}
    TableGroup(const char*, const Table&&) = delete;
};
template <class G, class Descriptor, class = void> struct IsTableGroup : std::false_type {};
template <class T, class Descriptor>
struct IsTableGroup<TableGroup<T>, Descriptor, std::void_t<typename T::Descriptor>>
    : std::is_same<typename T::Descriptor, Descriptor> {};
} // namespace detail

template <class Table, std::enable_if_t<detail::IsLocalTable<Table>::value, int> = 0>
constexpr auto group(const char* name, const Table& table) noexcept
{ return detail::TableGroup<Table>{name, table}; }
template <class Table, std::enable_if_t<detail::IsLocalTable<Table>::value, int> = 0>
auto group(const char*, const Table&&) = delete;
} // namespace telemetry
#endif
