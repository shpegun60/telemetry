/*
 * @file CommandCatalogs.hpp
 * @brief Command groups, native routing and positional encoded execution.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_COMMAND_COMMAND_CATALOGS_HPP
#define TELEMETRY_STRUCTURED_COMMAND_COMMAND_CATALOGS_HPP

#include "../model/Catalog.hpp"
#include "CommandTable.hpp"

namespace telemetry::structured {

struct CommandCatalog {
    const char* name;
    const CommandEntry* entries;
    std::uint32_t count;
};

class CommandIndex {
public:
    constexpr CommandIndex(const CommandCatalog* catalogs, std::uint32_t count) noexcept
        : catalogs_(catalogs), count_(count) {}
    [[nodiscard]] constexpr const CommandCatalog* catalogs() const noexcept { return catalogs_; }
    [[nodiscard]] constexpr std::uint32_t count() const noexcept { return count_; }

    template <class... Explicit, std::integral Id>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] constexpr const CommandEntry* find(Id id) const noexcept
    {
        if (!telemetry::detail::indexFits<PackedId>(id)) return nullptr;
        const auto packed = static_cast<PackedId>(id);
        const auto group = packed >> 16;
        const auto entry = packed & 0xffffu;
        if (group >= count_) return nullptr;
        const auto& catalog = catalogs_[group];
        return entry < catalog.count ? catalog.entries + entry : nullptr;
    }

    template <class... Explicit, std::integral Id>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] EncodedCommandResult executeEncoded(Id id, std::span<const std::byte> input,
                                                      Workspace& workspace) const noexcept
    {
        const auto* entry = find(id);
        if (entry == nullptr) return {DispatchStatus::NotFound};
        return entry->executeEncoded(input, workspace);
    }

    const CommandCatalog* catalogs_;
    std::uint32_t count_;
};

namespace model_detail {
template <class T> struct IsCommandTable : std::false_type {};
template <class... D> struct IsCommandTable<CommandTable<D...>> : std::true_type {};
}

template <class... Groups>
class CommandCatalogTable {
    static_assert(sizeof...(Groups) <= idComponentCapacity,
                  "Command catalog exceeds the 16-bit group position space");
    static_assert((model_detail::IsCommandTable<typename Groups::TableType>::value && ...),
                  "Command catalog groups require CommandTable instances");
public:
    using RootTypes = typename detail::ConcatLists<typename Groups::TableType::RootTypes...>::type;
    static constexpr std::size_t staticSize = sizeof...(Groups);
    constexpr explicit CommandCatalogTable(Groups... groups) noexcept
        : groups_(groups...), catalogs_(makeCatalogs(std::index_sequence_for<Groups...>{})) {}

    CommandCatalogTable(const CommandCatalogTable&) = delete;
    CommandCatalogTable& operator=(const CommandCatalogTable&) = delete;
    CommandCatalogTable(CommandCatalogTable&&) = delete;
    CommandCatalogTable& operator=(CommandCatalogTable&&) = delete;

    [[nodiscard]] constexpr CommandIndex index() const& noexcept
    { return {catalogs_.data(), static_cast<std::uint32_t>(staticSize)}; }
    CommandIndex index() const&& = delete;
    [[nodiscard]] constexpr const CommandCatalog* data() const& noexcept { return catalogs_.data(); }
    const CommandCatalog* data() const&& = delete;
    [[nodiscard]] constexpr std::size_t size() const noexcept { return staticSize; }

    [[nodiscard]] constexpr bool empty() const noexcept { return staticSize == 0; }
    [[nodiscard]] constexpr const CommandCatalog* begin() const& noexcept { return catalogs_.data(); }
    const CommandCatalog* begin() const&& = delete;
    [[nodiscard]] constexpr const CommandCatalog* end() const& noexcept
    {
        // std::array may expose checked iterators on other standard libraries.
        // Our view uses pointers and never adds zero to a possibly null data().
        if constexpr (staticSize == 0) return catalogs_.data();
        else return catalogs_.data() + staticSize;
    }
    const CommandCatalog* end() const&& = delete;
    [[nodiscard]] constexpr const CommandCatalog& operator[](std::size_t i) const& noexcept { return catalogs_[i]; }
    const CommandCatalog& operator[](std::size_t) const&& = delete;

    // Global typed access uses a packed ID, not a local position enum.
    template <auto Id>
    [[nodiscard]] constexpr decltype(auto) get() const& noexcept
    {
        constexpr auto packed = telemetry::detail::packedIdValue<Id>();
        constexpr auto group = packed >> 16;
        constexpr auto entry = packed & 0xffffu;
        static_assert(group < staticSize, "Command group is outside this catalog");
        if constexpr (group < staticSize)
            return std::get<group>(groups_).table->template get<entry>();
    }
    template <auto Id>
    void get() const&& = delete;

    template <class Visitor>
    constexpr void forEach(Visitor&& visitor) const&
    {
        detail::forEachGroup(groups_, visitor, std::index_sequence_for<Groups...>{});
    }
    template <class Visitor>
    void forEach(Visitor&&) const&& = delete;

    template <class... Explicit, std::integral Id, class Visitor>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] bool visit(Id id, Visitor&& visitor) const&
    {
        if (!telemetry::detail::indexFits<PackedId>(id)) return false;
        const auto packed = static_cast<PackedId>(id);
        const auto group = packed >> 16;
        if (group >= staticSize) return false;
        using Dispatch = detail::GroupDispatch<std::tuple<Groups...>,
                                               std::remove_reference_t<Visitor>>;
        return Dispatch::entries[group](groups_, packed & 0xffffu, visitor);
    }
    template <class... Explicit, std::integral Id, class Visitor>
        requires (sizeof...(Explicit) == 0)
    bool visit(Id, Visitor&&) const&& = delete;

    template <auto Id, class... Args>
    [[nodiscard]] telemetry::CommandResult call(Args&&... args) const noexcept
    {
        constexpr auto packed = telemetry::detail::packedIdValue<Id>();
        constexpr auto group = packed >> 16;
        constexpr auto entry = packed & 0xffffu;
        static_assert(group < staticSize, "Command group is outside this catalog");
        if constexpr (group < staticSize)
            return std::get<group>(groups_).table->template call<entry>(std::forward<Args>(args)...);
    }

    template <class Registry>
    struct TypeStorage {
        inline static constexpr std::array<ValueTypeCatalog, staticSize> catalogs{{
            ValueTypeCatalog{Groups::TableType::template TypeStorage<Registry>::entries.data(),
                             static_cast<std::uint32_t>(Groups::TableType::staticSize)}...}};
    };

private:
    template <std::size_t... I>
    constexpr auto makeCatalogs(std::index_sequence<I...>) noexcept
    {
        return std::array<CommandCatalog, staticSize>{{CommandCatalog{
            std::get<I>(groups_).name, std::get<I>(groups_).table->data(),
            static_cast<std::uint32_t>(std::get<I>(groups_).table->size())}...}};
    }
    std::tuple<Groups...> groups_;
    std::array<CommandCatalog, staticSize> catalogs_;
};

template <class... Groups>
CommandCatalogTable(Groups...) -> CommandCatalogTable<Groups...>;

} // namespace telemetry::structured

#endif
