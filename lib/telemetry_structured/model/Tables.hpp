/*
 * @file Tables.hpp
 * @brief Positional native Service table and its bounded encoded fallback.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_MODEL_TABLES_HPP
#define TELEMETRY_STRUCTURED_MODEL_TABLES_HPP

#include "Dispatch.hpp"
#include "Service.hpp"
#include "../codec/Codec.hpp"
#include "../type/Registry.hpp"

#include <telemetry/core/TelemetryId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry::structured {

struct ServiceTypePair {
    TypeId requestTypeId;
    TypeId responseTypeId;
};

// Only this erased view is needed by runtime dispatch. Its target points into
// its owning ServiceTable, which must outlive every catalog/index using it.
struct ServiceEntry {
    using Invoke = EncodedCallResult (*)(const void*, std::span<const std::byte>,
                                        std::span<std::byte>, Workspace&) noexcept;

    const void* definition;
    Invoke invoke;
    const char* name;
    std::uint32_t requestWireBytes;
    std::uint32_t responseWireBytes;
    std::uint32_t scratchBytes;
};

template <class... Definitions>
class ServiceTable {
    static_assert(sizeof...(Definitions) <= idComponentCapacity,
                  "Service table exceeds the 16-bit local position space");
    static_assert((std::is_same_v<Definitions,
                   ServiceDefinition<typename Definitions::BindingType>> && ...),
                  "ServiceTable requires service() definitions");

public:
    using RootTypes = typename detail::ConcatLists<
        detail::TypeList<typename Definitions::Request,
                         typename Definitions::Response>...>::type;

    static constexpr std::size_t staticSize = sizeof...(Definitions);

    constexpr explicit ServiceTable(Definitions... definitions) noexcept
        : definitions_(definitions...), entries_(makeEntries(std::index_sequence_for<Definitions...>{}))
    {}

    // Entries point into definitions_. A copied or moved table would retain
    // pointers to the old object, so those operations are intentionally absent.
    ServiceTable(const ServiceTable&) = delete;
    ServiceTable& operator=(const ServiceTable&) = delete;
    ServiceTable(ServiceTable&&) = delete;
    ServiceTable& operator=(ServiceTable&&) = delete;

    [[nodiscard]] constexpr const ServiceEntry* data() const& noexcept
    {
        return entries_.data();
    }
    const ServiceEntry* data() const&& = delete;
    [[nodiscard]] constexpr std::size_t size() const noexcept { return staticSize; }

    template <auto Position, class... Args>
    [[nodiscard]] decltype(auto) call(Args&&... args) const noexcept
    {
        constexpr auto index = telemetry::detail::positionValue<Position>();
        static_assert(index < staticSize, "Service position is outside this table");
        return std::get<static_cast<std::size_t>(index)>(definitions_)
            .call(std::forward<Args>(args)...);
    }

    template <class Registry>
    struct TypeStorage {
        inline static constexpr std::array<ServiceTypePair, staticSize> entries{{
            ServiceTypePair{Registry::template typeId<typename Definitions::Request>(),
                            Registry::template typeId<typename Definitions::Response>()}...
        }};
    };

private:
    template <class Definition>
    [[nodiscard]] static consteval std::uint32_t requiredScratch() noexcept
    {
        constexpr std::uint64_t requestBytes = [] {
            if constexpr (!std::is_void_v<typename Definition::Request>)
                return static_cast<std::uint64_t>(
                    scratchBytes<typename Definition::Request>);
            else
                return std::uint64_t{0};
        }();
        constexpr std::uint64_t resultBytes = [] {
            if constexpr (!std::is_void_v<typename Definition::Response>)
                return static_cast<std::uint64_t>(
                    scratchBytes<typename Definition::Result>);
            else
                return std::uint64_t{0};
        }();
        static_assert(requestBytes <= std::numeric_limits<std::uint32_t>::max() &&
                      resultBytes <= std::numeric_limits<std::uint32_t>::max() - requestBytes,
                      "Service scratch size overflows u32");
        return static_cast<std::uint32_t>(requestBytes + resultBytes);
    }

    template <class Definition, class RequestPointer>
    [[nodiscard]] static EncodedCallResult invokeReady(const Definition& definition,
                                                        RequestPointer request,
                                                        std::span<std::byte> output,
                                                        Workspace& workspace) noexcept
    {
        using Binding = typename Definition::BindingType;
        using Response = typename Definition::Response;
        using Result = typename Definition::Result;

        // Keep one snapshot for both the availability check and the call.
        auto selected = definition.binding_.snapshot();
        if (!Binding::available(selected))
            return {DispatchStatus::Unavailable, ServiceStatus::Ok, 0};

        if constexpr (std::is_void_v<Response>) {
            Result result = [&]() -> Result {
                if constexpr (std::is_void_v<typename Definition::Request>)
                    return Definition::invokeSelected(selected);
                else
                    return Definition::invokeSelected(selected, *request);
            }();
            return {DispatchStatus::Ok, result.status(), 0};
        } else {
            auto resultLease = workspace.reserve<Result>();
            if (!resultLease.valid())
                return {DispatchStatus::WorkspaceTooSmall, ServiceStatus::Ok, 0};
            Result* result = resultLease.constructFrom([&]() -> Result {
                if constexpr (std::is_void_v<typename Definition::Request>)
                    return Definition::invokeSelected(selected);
                else
                    return Definition::invokeSelected(selected, *request);
            });
            if (result == nullptr)
                return {DispatchStatus::InternalError, ServiceStatus::Ok, 0};
            if (!result->hasValue())
                return {DispatchStatus::Ok, result->status(), 0};

            constexpr std::uint32_t bytes = wireSize<Response>;
            const auto status = encode(*result->valueOrNull(), output.first(bytes));
            if (status != CodecStatus::Ok)
                return {DispatchStatus::InternalError, ServiceStatus::Ok, 0};
            return {DispatchStatus::Ok, ServiceStatus::Ok, bytes};
        }
    }

    template <class Definition>
    [[nodiscard]] static EncodedCallResult invokeOne(const void* raw,
                                                      std::span<const std::byte> input,
                                                      std::span<std::byte> output,
                                                      Workspace& workspace) noexcept
    {
        using Request = typename Definition::Request;
        using Response = typename Definition::Response;

        // All caller-controlled sizes and overlap are checked before a target
        // is resolved. A malformed bool is rejected by decode before callback.
        if (input.size() != wireSize<Request>)
            return {DispatchStatus::InvalidPayload, ServiceStatus::Ok, 0};
        if (output.size() < wireSize<Response>)
            return {DispatchStatus::BufferTooSmall, ServiceStatus::Ok, 0};
        if (!buffersDisjoint(input, output, workspace.storage()))
            return {DispatchStatus::InvalidPayload, ServiceStatus::Ok, 0};
        constexpr std::uint32_t required = requiredScratch<Definition>();
        if (workspace.used() > workspace.storage().size() ||
            workspace.storage().size() - workspace.used() < required)
            return {DispatchStatus::WorkspaceTooSmall, ServiceStatus::Ok, 0};

        const auto& definition = *static_cast<const Definition*>(raw);
        if constexpr (std::is_void_v<Request>) {
            return invokeReady(definition, static_cast<const void*>(nullptr), output, workspace);
        } else {
            auto requestLease = workspace.reserve<Request>();
            if (!requestLease.valid())
                return {DispatchStatus::WorkspaceTooSmall, ServiceStatus::Ok, 0};
            Request* request = nullptr;
            const auto status = decode(input, requestLease, request);
            if (status == CodecStatus::InvalidValue)
                return {DispatchStatus::InvalidPayload, ServiceStatus::Ok, 0};
            if (status != CodecStatus::Ok || request == nullptr)
                return {DispatchStatus::InternalError, ServiceStatus::Ok, 0};
            return invokeReady(definition, request, output, workspace);
        }
    }

    template <std::size_t... I>
    [[nodiscard]] constexpr std::array<ServiceEntry, staticSize>
    makeEntries(std::index_sequence<I...>) noexcept
    {
        return {{ServiceEntry{
            &std::get<I>(definitions_),
            &invokeOne<std::tuple_element_t<I, std::tuple<Definitions...>>>,
            std::get<I>(definitions_).name(),
            wireSize<typename std::tuple_element_t<I, std::tuple<Definitions...>>::Request>,
            wireSize<typename std::tuple_element_t<I, std::tuple<Definitions...>>::Response>,
            requiredScratch<std::tuple_element_t<I, std::tuple<Definitions...>>>()}...}};
    }

    std::tuple<Definitions...> definitions_;
    std::array<ServiceEntry, staticSize> entries_;
};

template <class... Definitions>
ServiceTable(Definitions...) -> ServiceTable<Definitions...>;

} // namespace telemetry::structured

#endif
