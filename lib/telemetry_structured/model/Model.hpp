/*
 * @file Model.hpp
 * @brief One type registry and runtime views for structured endpoint catalogs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_MODEL_MODEL_HPP
#define TELEMETRY_STRUCTURED_MODEL_MODEL_HPP

#include "Catalogs.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace telemetry::structured {

// Stage 07 has Services first. Fields and Commands join these two positions
// in Stage 08 without changing Model's three-catalog construction pattern.
struct EmptyEndpointCatalog {
    using RootTypes = detail::TypeList<>;
};
inline constexpr EmptyEndpointCatalog emptyFields{};
inline constexpr EmptyEndpointCatalog emptyCommands{};

struct ModelView {
    TypeRegistryView types;
    ServiceIndex services;
    const ServiceTypeCatalog* serviceTypes;
    std::uint32_t serviceCatalogCount;

    template <class... Explicit, std::integral Id>
        requires (sizeof...(Explicit) == 0)
    [[nodiscard]] constexpr std::optional<ServiceTypePair> serviceTypeIds(Id id) const noexcept
    {
        if (!telemetry::detail::indexFits<PackedId>(id)) return std::nullopt;
        const auto packed = static_cast<PackedId>(id);
        const auto groupPosition = static_cast<std::uint32_t>(packed >> 16);
        const auto entryPosition = static_cast<std::uint32_t>(packed & 0xffffu);
        if (groupPosition >= serviceCatalogCount) return std::nullopt;
        const auto& catalog = serviceTypes[groupPosition];
        if (entryPosition >= catalog.count) return std::nullopt;
        return catalog.entries[entryPosition];
    }
};

template <class Fields, class Commands, class Services>
class Model {
    using AllRoots = typename detail::ConcatLists<typename Fields::RootTypes,
                                                    typename Commands::RootTypes,
                                                    typename Services::RootTypes>::type;

public:
    using Registry = typename detail::RegistryFromList<AllRoots>::type;

    constexpr Model(const Fields& fields, const Commands& commands,
                    const Services& services) noexcept
        : fields_(&fields), commands_(&commands), services_(&services)
    {}

    template <class F, class C, class S>
        requires (!std::is_lvalue_reference_v<F> ||
                  !std::is_lvalue_reference_v<C> ||
                  !std::is_lvalue_reference_v<S>)
    Model(F&&, C&&, S&&) = delete;

    [[nodiscard]] static constexpr TypeRegistryView types() noexcept
    {
        return Registry::view();
    }

    template <class T>
    [[nodiscard]] static consteval TypeId typeId() noexcept
    {
        return Registry::template typeId<T>();
    }

    [[nodiscard]] constexpr ServiceIndex serviceIndex() const noexcept
    {
        return services_->index();
    }

    [[nodiscard]] constexpr ModelView view() const noexcept
    {
        return {types(), serviceIndex(),
                Services::template TypeStorage<Registry>::catalogs.data(),
                static_cast<std::uint32_t>(services_->size())};
    }

    [[nodiscard]] constexpr std::uint32_t maxServiceScratch() const noexcept
    {
        std::uint32_t result = 0;
        const auto index = serviceIndex();
        for (std::uint32_t group = 0; group < index.count(); ++group) {
            const auto& catalog = index.catalogs()[group];
            for (std::uint32_t entry = 0; entry < catalog.count; ++entry)
                if (catalog.entries[entry].scratchBytes > result)
                    result = catalog.entries[entry].scratchBytes;
        }
        return result;
    }

    [[nodiscard]] constexpr std::uint32_t maxServiceResponseWireSize() const noexcept
    {
        std::uint32_t result = 0;
        const auto index = serviceIndex();
        for (std::uint32_t group = 0; group < index.count(); ++group) {
            const auto& catalog = index.catalogs()[group];
            for (std::uint32_t entry = 0; entry < catalog.count; ++entry)
                if (catalog.entries[entry].responseWireBytes > result)
                    result = catalog.entries[entry].responseWireBytes;
        }
        return result;
    }

private:
    const Fields* fields_;
    const Commands* commands_;
    const Services* services_;
};

template <class Fields, class Commands, class Services>
Model(const Fields&, const Commands&, const Services&) -> Model<Fields, Commands, Services>;

} // namespace telemetry::structured

#endif
