/*
 * @file ServiceCatalogs.hpp
 * @brief Borrowed Service groups, compile-time routing and O(1) ID lookup.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_SERVICE_SERVICE_CATALOGS_HPP
#define TELEMETRY_STRUCTURED_SERVICE_SERVICE_CATALOGS_HPP

#include "ServiceTable.hpp"
#include "../model/Catalog.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry::structured {

namespace model_detail {

template <class Table>
struct IsServiceTable : std::false_type {};

template <class... Definitions>
struct IsServiceTable<ServiceTable<Definitions...>> : std::true_type {};

} // namespace model_detail

struct ServiceCatalog {
    const char* name;
    const ServiceEntry* entries;
    std::uint32_t count;
};

struct ServiceTypeCatalog {
    const ServiceTypePair* entries;
    std::uint32_t count;
};

class ServiceIndex {
public:
    constexpr ServiceIndex(const ServiceCatalog* catalogs, std::uint32_t count) noexcept
        : catalogs_(catalogs), count_(count)
    {}

    [[nodiscard]] constexpr const ServiceCatalog* catalogs() const noexcept
    {
        return catalogs_;
    }
    [[nodiscard]] constexpr std::uint32_t count() const noexcept { return count_; }

    template <class... Explicit, std::integral Id>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] constexpr const ServiceEntry* find(Id id) const noexcept
    {
        if (!telemetry::detail::indexFits<PackedId>(id)) return nullptr;
        const auto packed = static_cast<PackedId>(id);
        const auto groupPosition = static_cast<std::uint32_t>(packed >> 16);
        const auto entryPosition = static_cast<std::uint32_t>(packed & 0xffffu);
        if (groupPosition >= count_) return nullptr;
        const auto& catalog = catalogs_[groupPosition];
        return entryPosition < catalog.count ? catalog.entries + entryPosition : nullptr;
    }

    template <class... Explicit, std::integral Id>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] EncodedCallResult callEncoded(Id id,
                                                std::span<const std::byte> input,
                                                std::span<std::byte> output,
                                                Workspace& workspace) const noexcept
    {
        const ServiceEntry* entry = find(id);
        if (entry == nullptr) return {DispatchStatus::NotFound, ServiceStatus::Ok, 0};
        return entry->callEncoded(input, output, workspace);
    }

public:
    // This is a borrowed runtime view. The members are public so the exact
    // cross-translation-unit ABI tag can cover both offsets.
    const ServiceCatalog* catalogs_;
    std::uint32_t count_;
};

template <class... Groups>
class ServiceCatalogTable {
    static_assert(sizeof...(Groups) <= idComponentCapacity,
                  "Service catalog exceeds the 16-bit group position space");
    static_assert((model_detail::IsServiceTable<typename Groups::TableType>::value && ...),
                  "Service catalog groups require ServiceTable instances");

public:
    using RootTypes = typename detail::ConcatLists<
        typename Groups::TableType::RootTypes...>::type;
    static constexpr std::size_t staticSize = sizeof...(Groups);

    constexpr explicit ServiceCatalogTable(Groups... groups) noexcept
        : groups_(groups...), catalogs_(makeCatalogs(std::index_sequence_for<Groups...>{}))
    {}

    ServiceCatalogTable(const ServiceCatalogTable&) = delete;
    ServiceCatalogTable& operator=(const ServiceCatalogTable&) = delete;
    ServiceCatalogTable(ServiceCatalogTable&&) = delete;
    ServiceCatalogTable& operator=(ServiceCatalogTable&&) = delete;

    [[nodiscard]] constexpr ServiceIndex index() const& noexcept
    {
        return {catalogs_.data(), static_cast<std::uint32_t>(staticSize)};
    }
    ServiceIndex index() const&& = delete;

    [[nodiscard]] constexpr const ServiceCatalog* data() const& noexcept
    {
        return catalogs_.data();
    }
    const ServiceCatalog* data() const&& = delete;
    [[nodiscard]] constexpr std::size_t size() const noexcept { return staticSize; }

    [[nodiscard]] constexpr bool empty() const noexcept { return staticSize == 0; }
    [[nodiscard]] constexpr const ServiceCatalog* begin() const& noexcept { return catalogs_.data(); }
    const ServiceCatalog* begin() const&& = delete;
    [[nodiscard]] constexpr const ServiceCatalog* end() const& noexcept
    {
        // std::array may expose checked iterators on other standard libraries.
        // Our view uses pointers and never adds zero to a possibly null data().
        if constexpr (staticSize == 0) return catalogs_.data();
        else return catalogs_.data() + staticSize;
    }
    const ServiceCatalog* end() const&& = delete;
    [[nodiscard]] constexpr const ServiceCatalog& operator[](std::size_t i) const& noexcept { return catalogs_[i]; }
    const ServiceCatalog& operator[](std::size_t) const&& = delete;

    // Global typed access uses a packed ID, not a local position enum.
    template <auto Id>
    [[nodiscard]] constexpr decltype(auto) get() const& noexcept
    {
        constexpr auto packed = telemetry::detail::packedIdValue<Id>();
        constexpr auto group = packed >> 16;
        constexpr auto entry = packed & 0xffffu;
        static_assert(group < staticSize, "Service group is outside this catalog");
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
    [[nodiscard]] decltype(auto) call(Args&&... args) const noexcept
    {
        constexpr PackedId packed = telemetry::detail::packedIdValue<Id>();
        constexpr std::size_t groupPosition = packed >> 16;
        constexpr std::size_t entryPosition = packed & 0xffffu;
        static_assert(groupPosition < staticSize, "Service group is outside this catalog");
        if constexpr (groupPosition < staticSize) {
            return std::get<groupPosition>(groups_).table
                ->template call<entryPosition>(std::forward<Args>(args)...);
        }
    }

    template <class Registry>
    struct TypeStorage {
        inline static constexpr std::array<ServiceTypeCatalog, staticSize> catalogs{{
            ServiceTypeCatalog{
                Groups::TableType::template TypeStorage<Registry>::entries.data(),
                static_cast<std::uint32_t>(Groups::TableType::staticSize)}...
        }};
    };

private:
    template <std::size_t... I>
    [[nodiscard]] constexpr std::array<ServiceCatalog, staticSize>
    makeCatalogs(std::index_sequence<I...>) noexcept
    {
        return {{ServiceCatalog{std::get<I>(groups_).name,
                                std::get<I>(groups_).table->data(),
                                static_cast<std::uint32_t>(
                                    std::get<I>(groups_).table->size())}...}};
    }

    std::tuple<Groups...> groups_;
    std::array<ServiceCatalog, staticSize> catalogs_;
};

template <class... Groups>
ServiceCatalogTable(Groups...) -> ServiceCatalogTable<Groups...>;

} // namespace telemetry::structured

#endif
