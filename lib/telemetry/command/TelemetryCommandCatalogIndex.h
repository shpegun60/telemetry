/**
 * @file TelemetryCommandCatalogIndex.h
 * @brief Constant-time lookup across dense command catalogs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_CATALOG_INDEX_H
#define TELEMETRY_COMMAND_CATALOG_INDEX_H

#include "TelemetryCommandCatalog.h"
#include "TelemetryCommandCatalogView.h"

namespace telemetry {

// A non-owning runtime view. The two bounds checks precede pointer arithmetic;
// catalog construction guarantees that a null rows pointer has zero count.
class CommandCatalogIndex {
public:
    constexpr CommandCatalogIndex() noexcept = default;

    template <class = void>
    constexpr CommandCatalogIndex(const CommandCatalog* catalogs,
                                  std::size_t requestedCount) noexcept
        : catalogs_(catalogs), count_(detail::pointerPresent(catalogs)
              ? (requestedCount < idComponentCapacity
                    ? requestedCount : idComponentCapacity) : 0)
    {
    }

    template <std::size_t N>
    constexpr explicit CommandCatalogIndex(const CommandCatalog (&catalogs)[N]) noexcept
        : CommandCatalogIndex(static_cast<const CommandCatalog*>(catalogs), N) {}

    template <std::size_t N>
    CommandCatalogIndex(const CommandCatalog (&&)[N]) = delete;
    template <std::size_t N>
    CommandCatalogIndex(const CommandCatalog (&&)[N], std::size_t) = delete;

    TELEMETRY_FORCE_INLINE constexpr const Command* find(CommandId id) const noexcept
    {
        const std::size_t group = groupOf(id);
        if (group >= count_) return nullptr;
        const CommandCatalog& catalog = catalogs_[group];
        const std::size_t index = indexOf(id);
        return index < catalog.count ? catalog.commands + index : nullptr;
    }

    template <class... Explicit, class Id, std::enable_if_t<sizeof...(Explicit) == 0
        && detail::isPackedIdInput<Id> && !std::is_same_v<Id, CommandId>, int> = 0>
    TELEMETRY_FORCE_INLINE constexpr const Command* find(Id id) const noexcept
    {
        return detail::indexFits<CommandId>(id)
            ? find(static_cast<CommandId>(id)) : nullptr;
    }
    template <class... Explicit, class Id,
              std::enable_if_t<sizeof...(Explicit) == 0 && !detail::isPackedIdInput<Id>, int> = 0>
    const Command* find(const Id&) const = delete;

    constexpr const CommandCatalog* catalog(GroupId group) const noexcept
    {
        return group < count_ ? catalogs_ + group : nullptr;
    }

    template <class... Explicit, class Group, std::enable_if_t<sizeof...(Explicit) == 0
        && detail::isIdInput<Group> && !std::is_same_v<Group, GroupId>, int> = 0>
    constexpr const CommandCatalog* catalog(Group group) const noexcept
    {
        return detail::indexFits<GroupId>(group)
            ? catalog(static_cast<GroupId>(group)) : nullptr;
    }
    template <class... Explicit, class Group,
              std::enable_if_t<sizeof...(Explicit) == 0 && !detail::isIdInput<Group>, int> = 0>
    const CommandCatalog* catalog(const Group&) const = delete;

    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult execute(CommandId id, const Scalar* values, std::size_t count) const noexcept
    {
        const Command* command = find(id);
        return command != nullptr ? command->execute(values, count) : CommandResult::NotFound;
    }

    template <class... Explicit, class Id, std::enable_if_t<sizeof...(Explicit) == 0
        && detail::isPackedIdInput<Id> && !std::is_same_v<Id, CommandId>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult execute(Id id, const Scalar* values, std::size_t count) const noexcept
    {
        const Command* command = find(id);
        return command != nullptr ? command->execute(values, count) : CommandResult::NotFound;
    }
    template <class... Explicit, class Id,
              std::enable_if_t<sizeof...(Explicit) == 0 && !detail::isPackedIdInput<Id>, int> = 0>
    CommandResult execute(const Id&, const Scalar*, std::size_t) const = delete;

    template <class... Explicit, class... A,
              std::enable_if_t<sizeof...(Explicit) == 0, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto call(CommandId id, A... values) const noexcept
        -> decltype(std::declval<const Command&>().call(values...))
    {
        // Native-looking arguments are normalized by Command::call here.
        // Compile-time target routing is provided by CommandCatalogTable.
        const Command* command = find(id);
        return command != nullptr ? command->call(values...) : CommandResult::NotFound;
    }

    template <class... Explicit, class Id, class... A,
              std::enable_if_t<sizeof...(Explicit) == 0 && detail::isPackedIdInput<Id>
                  && !std::is_same_v<Id, CommandId>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto call(Id id, A... values) const noexcept
        -> decltype(std::declval<const Command&>().call(values...))
    {
        const Command* command = find(id);
        return command != nullptr ? command->call(values...) : CommandResult::NotFound;
    }
    template <class... Explicit, class Id, class... A,
              std::enable_if_t<sizeof...(Explicit) == 0 && !detail::isPackedIdInput<Id>, int> = 0>
    CommandResult call(const Id&, A...) const = delete;

    constexpr const CommandCatalog* data() const noexcept { return catalogs_; }
    constexpr std::size_t size() const noexcept { return count_; }
    constexpr bool empty() const noexcept { return count_ == 0; }
    constexpr const CommandCatalog* begin() const noexcept { return catalogs_; }
    constexpr const CommandCatalog* end() const noexcept { return detail::pointerPresent(catalogs_) ? catalogs_ + count_ : nullptr; }
    constexpr CommandCatalogRange catalogs() const noexcept
    { return CommandCatalogRange::fromCapped(catalogs_, count_, 0); }

    static constexpr std::size_t abiCatalogsOffset() noexcept;
    static constexpr std::size_t abiCountOffset() noexcept;

private:
    const CommandCatalog* catalogs_ = nullptr;
    std::size_t count_ = 0;
};

constexpr std::size_t CommandCatalogIndex::abiCatalogsOffset() noexcept
{ return offsetof(CommandCatalogIndex, catalogs_); }
constexpr std::size_t CommandCatalogIndex::abiCountOffset() noexcept
{ return offsetof(CommandCatalogIndex, count_); }

} // namespace telemetry

#endif
