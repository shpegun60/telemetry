/*
 * @file ServiceTable.hpp
 * @brief Positional native Service table and its bounded encoded fallback.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_SERVICE_SERVICE_TABLE_HPP
#define TELEMETRY_STRUCTURED_SERVICE_SERVICE_TABLE_HPP

#include "../result/Dispatch.hpp"
#include "Service.hpp"
#include "../detail/Encoded.hpp"
#include "../detail/Traversal.hpp"
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

// Only this erased view is needed by runtime dispatch. Its context borrows
// the owner/slot or table-owned binding; both must outlive the view.
struct ServiceEntry {
    // Call callEncoded for checked access. The internal thunk consumes exact
    // wire buffers, whose compile-time extents are supplied by its own type.
    using Invoke = EncodedCallResult (*)(const void*, const std::byte*,
                                        std::byte*, Workspace&) noexcept;

    const void* context;
    Invoke invoke;
    const char* name;
    std::uint32_t requestWireBytes;
    std::uint32_t responseWireBytes;
    std::uint32_t scratchBytes;

    [[nodiscard]] EncodedCallResult callEncoded(std::span<const std::byte> input,
                                                std::span<std::byte> output,
                                                Workspace& workspace) const noexcept
    {
        if (input.size() != requestWireBytes)
            return {DispatchStatus::InvalidPayload, ServiceStatus::Ok, 0};
        if (output.size() < responseWireBytes)
            return {DispatchStatus::BufferTooSmall, ServiceStatus::Ok, 0};
        // Request decoding finishes before the first response byte is written.
        // Input and output may therefore share storage, including partial overlap.
        if (scratchBytes != 0) {
            if (buffersOverlap(input, workspace.storage()) || buffersOverlap(output, workspace.storage()))
                return {DispatchStatus::InvalidPayload, ServiceStatus::Ok, 0};
            if (workspace.used() > workspace.storage().size() ||
                workspace.storage().size() - workspace.used() < scratchBytes)
                return {DispatchStatus::WorkspaceTooSmall, ServiceStatus::Ok, 0};
        }
        return invoke(context, input.data(), output.data(), workspace);
    }
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

    // Runtime functions and custom bindings can retain pointers into definitions_.
    // Moving/copying a table would invalidate those contexts.
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

    // Erased iteration borrows this table; typed traversal below borrows its
    // exact definitions. Neither interface stores or materializes a value.
    [[nodiscard]] constexpr bool empty() const noexcept { return staticSize == 0; }
    [[nodiscard]] constexpr const ServiceEntry* begin() const& noexcept { return entries_.data(); }
    const ServiceEntry* begin() const&& = delete;
    [[nodiscard]] constexpr const ServiceEntry* end() const& noexcept
    {
        // std::array may expose checked iterators on other standard libraries.
        // Our view uses pointers and never adds zero to a possibly null data().
        if constexpr (staticSize == 0) return entries_.data();
        else return entries_.data() + staticSize;
    }
    const ServiceEntry* end() const&& = delete;
    [[nodiscard]] constexpr const ServiceEntry& operator[](std::size_t i) const& noexcept { return entries_[i]; }
    const ServiceEntry& operator[](std::size_t) const&& = delete;

    template <auto Position>
    [[nodiscard]] constexpr decltype(auto) get() const& noexcept
    {
        constexpr auto i = telemetry::detail::positionValue<Position>();
        static_assert(i < staticSize, "Service position is outside this table");
        if constexpr (i < staticSize) return std::get<static_cast<std::size_t>(i)>(definitions_);
    }
    template <auto Position>
    void get() const&& = delete;

    template <class Visitor>
    constexpr void forEach(Visitor&& visitor) const&
    {
        detail::forEachDefinition(definitions_, visitor, std::index_sequence_for<Definitions...>{});
    }
    template <class Visitor>
    void forEach(Visitor&&) const&& = delete;

    // A local position may be an integer or scoped position enum. Invalid
    // positions return false without invoking the visitor. Callback returns
    // are ignored; the bool reports selection, not the endpoint's status.
    template <class... Explicit, class Position, class Visitor>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
    [[nodiscard]] bool visit(Position position, Visitor&& visitor) const&
    {
        if (!telemetry::detail::indexFits<std::uint32_t>(position)) return false;
        const auto i = static_cast<std::uint32_t>(position);
        if (i >= staticSize) return false;
        using Dispatch = detail::DefinitionDispatch<std::tuple<Definitions...>,
                                                    std::remove_reference_t<Visitor>>;
        Dispatch::entries[i](definitions_, visitor);
        return true;
    }
    template <class... Explicit, class Position, class Visitor>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
    bool visit(Position, Visitor&&) const&& = delete;

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
    using Storage = detail::ServiceStorage<typename Definition::Request,
        std::conditional_t<std::is_void_v<typename Definition::Response>,
                           void, typename Definition::Result>>;

    template <class Definition>
    [[nodiscard]] static consteval std::uint32_t requiredScratch() noexcept
    {
        constexpr std::uint64_t requestBytes = [] {
            if constexpr (!std::is_void_v<typename Definition::Request> &&
                          !Storage<Definition>::requestLocal)
                return static_cast<std::uint64_t>(
                    scratchBytes<typename Definition::Request>);
            else
                return std::uint64_t{0};
        }();
        constexpr std::uint64_t resultBytes = [] {
            if constexpr (!std::is_void_v<typename Definition::Response> &&
                          !Storage<Definition>::resultLocal)
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

    template <class Definition>
    [[nodiscard]] static EncodedCallResult encodeResult(
        const typename Definition::Result& result, std::span<std::byte> output) noexcept
    {
        if (!result.hasValue()) return {DispatchStatus::Ok, result.status(), 0};
        constexpr auto bytes = wireSize<typename Definition::Response>;
        // The entry boundary checked output capacity and any Workspace overlap.
        // The response lives either there or in this call's local object.
        detail::encodeEndpoint(*result.valueOrNull(), output.first(bytes));
        return {DispatchStatus::Ok, ServiceStatus::Ok, bytes};
    }

    template <class Definition, class RequestPointer>
    [[nodiscard]] static EncodedCallResult invokeReady(const void* context,
                                                        RequestPointer request,
                                                        std::span<std::byte> output,
                                                        Workspace& workspace) noexcept
    {
        using Binding = typename Definition::BindingType;
        using Response = typename Definition::Response;
        using Result = typename Definition::Result;

        // Keep one snapshot for both the availability check and the call.
        auto selected = detail::ErasedBinding<Binding>::snapshot(context);
        if (!Binding::available(selected))
            return {DispatchStatus::Unavailable, ServiceStatus::Ok, 0};

        if constexpr (std::is_void_v<Response>) {
            Result result = [&]() -> Result {
                if constexpr (std::is_void_v<typename Definition::Request>)
                    return Definition::template invokeSelected<true>(selected);
                else
                    return Definition::template invokeSelected<true>(selected, *request);
            }();
            return {DispatchStatus::Ok, result.status(), 0};
        } else if constexpr (Storage<Definition>::resultLocal) {
            const Result result = [&]() -> Result {
                if constexpr (std::is_void_v<typename Definition::Request>)
                    return Definition::template invokeSelected<true>(selected);
                else
                    return Definition::template invokeSelected<true>(selected, *request);
            }();
            return encodeResult<Definition>(result, output);
        } else {
            auto resultLease = workspace.reserve<Result>();
            if (!resultLease.valid())
                return {DispatchStatus::WorkspaceTooSmall, ServiceStatus::Ok, 0};
            Result* result = resultLease.constructFrom([&]() -> Result {
                if constexpr (std::is_void_v<typename Definition::Request>)
                    return Definition::template invokeSelected<true>(selected);
                else
                    return Definition::template invokeSelected<true>(selected, *request);
            });
            if (result == nullptr)
                return {DispatchStatus::InternalError, ServiceStatus::Ok, 0};
            return encodeResult<Definition>(*result, output);
        }
    }

    template <class Definition>
    [[nodiscard]] static EncodedCallResult invokeOne(const void* raw,
                                                      const std::byte* inputBytes,
                                                      std::byte* outputBytes,
                                                      Workspace& workspace) noexcept
    {
        using Request = typename Definition::Request;
        using Response = typename Definition::Response;

        // Length/overlap preflight is complete. Bool representation is still
        // checked by this typed decoder before resolving the callback target.
        const std::span<const std::byte> input{inputBytes, wireSize<Request>};
        const std::span<std::byte> output{outputBytes, wireSize<Response>};

        if constexpr (std::is_void_v<Request>) {
            return invokeReady<Definition>(raw, static_cast<const void*>(nullptr), output, workspace);
        } else if constexpr (Storage<Definition>::requestLocal) {
            if (!detail::validEndpoint<Request>(input))
                return {DispatchStatus::InvalidPayload, ServiceStatus::Ok, 0};
            const Request request = detail::decodeLocalEndpoint<Request>(input);
            return invokeReady<Definition>(raw, &request, output, workspace);
        } else {
            auto requestLease = workspace.reserve<Request>();
            if (!requestLease.valid())
                return {DispatchStatus::WorkspaceTooSmall, ServiceStatus::Ok, 0};
            Request* request = detail::decodeEndpoint<Request>(input, requestLease);
            if (request == nullptr)
                return {DispatchStatus::InvalidPayload, ServiceStatus::Ok, 0};
            return invokeReady<Definition>(raw, request, output, workspace);
        }
    }

    template <std::size_t... I>
    [[nodiscard]] constexpr std::array<ServiceEntry, staticSize>
    makeEntries(std::index_sequence<I...>) noexcept
    {
        return {{ServiceEntry{
            detail::ErasedBinding<typename std::tuple_element_t<I, std::tuple<Definitions...>>::BindingType>::context(
                std::get<I>(definitions_).binding_),
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
