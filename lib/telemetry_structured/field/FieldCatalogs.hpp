/*
 * @file FieldCatalogs.hpp
 * @brief Field groups, native routing and positional encoded access.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_FIELD_FIELD_CATALOGS_HPP
#define TELEMETRY_STRUCTURED_FIELD_FIELD_CATALOGS_HPP

#include "../model/Catalog.hpp"
#include "FieldTable.hpp"

namespace telemetry::structured {

struct FieldCatalog {
    const char* name;
    const FieldEntry* entries;
    std::uint32_t count;
};

class FieldIndex {
public:
    constexpr FieldIndex(const FieldCatalog* catalogs, std::uint32_t count) noexcept
        : catalogs_(catalogs), count_(count) {}
    [[nodiscard]] constexpr const FieldCatalog* catalogs() const noexcept { return catalogs_; }
    [[nodiscard]] constexpr std::uint32_t count() const noexcept { return count_; }

    template <class... Explicit, std::integral Id>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] constexpr const FieldEntry* find(Id id) const noexcept
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
    [[nodiscard]] EncodedReadResult readEncoded(Id id, std::span<std::byte> output,
                                                Workspace& workspace) const noexcept
    {
        const auto* entry = find(id);
        if (entry == nullptr) return {DispatchStatus::NotFound, 0};
        return entry->readEncoded(output, workspace);
    }

    template <class... Explicit, std::integral Id>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] EncodedWriteResult writeEncoded(Id id, std::span<const std::byte> input,
                                                  Workspace& workspace) const noexcept
    {
        const auto* entry = find(id);
        if (entry == nullptr) return {DispatchStatus::NotFound};
        return entry->writeEncoded(input, workspace);
    }

    // Public layout is recorded by the compiled adapter's exact ABI tag.
    const FieldCatalog* catalogs_;
    std::uint32_t count_;
};

namespace model_detail {
template <class T> struct IsFieldTable : std::false_type {};
template <class... D> struct IsFieldTable<FieldTable<D...>> : std::true_type {};
}

template <class... Groups>
class FieldCatalogTable {
    static_assert(sizeof...(Groups) <= idComponentCapacity,
                  "Field catalog exceeds the 16-bit group position space");
    static_assert((model_detail::IsFieldTable<typename Groups::TableType>::value && ...),
                  "Field catalog groups require FieldTable instances");
public:
    using RootTypes = typename detail::ConcatLists<typename Groups::TableType::RootTypes...>::type;
    static constexpr std::size_t staticSize = sizeof...(Groups);
    constexpr explicit FieldCatalogTable(Groups... groups) noexcept
        : groups_(groups...), catalogs_(makeCatalogs(std::index_sequence_for<Groups...>{})) {}

    FieldCatalogTable(const FieldCatalogTable&) = delete;
    FieldCatalogTable& operator=(const FieldCatalogTable&) = delete;
    FieldCatalogTable(FieldCatalogTable&&) = delete;
    FieldCatalogTable& operator=(FieldCatalogTable&&) = delete;

    [[nodiscard]] constexpr FieldIndex index() const& noexcept
    { return {catalogs_.data(), static_cast<std::uint32_t>(staticSize)}; }
    FieldIndex index() const&& = delete;
    [[nodiscard]] constexpr const FieldCatalog* data() const& noexcept { return catalogs_.data(); }
    const FieldCatalog* data() const&& = delete;
    [[nodiscard]] constexpr std::size_t size() const noexcept { return staticSize; }

    template <auto Id>
    [[nodiscard]] auto read() const noexcept
    {
        constexpr auto packed = telemetry::detail::packedIdValue<Id>();
        constexpr auto group = packed >> 16;
        constexpr auto entry = packed & 0xffffu;
        static_assert(group < staticSize, "Field group is outside this catalog");
        if constexpr (group < staticSize) return std::get<group>(groups_).table->template read<entry>();
    }

    template <auto Id, class Argument>
    [[nodiscard]] telemetry::WriteResult write(Argument&& value) const noexcept
    {
        constexpr auto packed = telemetry::detail::packedIdValue<Id>();
        constexpr auto group = packed >> 16;
        constexpr auto entry = packed & 0xffffu;
        static_assert(group < staticSize, "Field group is outside this catalog");
        if constexpr (group < staticSize)
            return std::get<group>(groups_).table->template write<entry>(std::forward<Argument>(value));
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
        return std::array<FieldCatalog, staticSize>{{FieldCatalog{
            std::get<I>(groups_).name, std::get<I>(groups_).table->data(),
            static_cast<std::uint32_t>(std::get<I>(groups_).table->size())}...}};
    }
    std::tuple<Groups...> groups_;
    std::array<FieldCatalog, staticSize> catalogs_;
};

template <class... Groups>
FieldCatalogTable(Groups...) -> FieldCatalogTable<Groups...>;

} // namespace telemetry::structured

#endif
