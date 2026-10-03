/*
 * @file CommandTable.hpp
 * @brief Native command calls and checked encoded execution with caller scratch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_COMMAND_COMMAND_TABLE_HPP
#define TELEMETRY_STRUCTURED_COMMAND_COMMAND_TABLE_HPP

#include "Command.hpp"
#include "../result/EndpointResults.hpp"
#include "../detail/Encoded.hpp"
#include "../detail/Traversal.hpp"
#include "../type/Registry.hpp"
#include <telemetry/core/Id.hpp>
#include <array>

namespace telemetry {

struct CommandEntry {
    // The raw operation is internal; executeEncoded checks the byte boundary.
    using Invoke = EncodedCommandResult (*)(const void*, const std::byte*, Workspace&) noexcept;
    const void* context;
    Invoke invoke;
    const char* name;
    std::uint32_t requestWireBytes;
    std::uint32_t scratchBytes;

    [[nodiscard]] EncodedCommandResult executeEncoded(std::span<const std::byte> input,
                                                       Workspace& workspace) const noexcept
    {
        if (input.size() != requestWireBytes) return {DispatchStatus::InvalidPayload};
        if (scratchBytes != 0 && buffersOverlap(input, workspace.storage()))
            return {DispatchStatus::InvalidPayload};
        return invoke(context, input.data(), workspace);
    }
};

template <class... Definitions>
class CommandTable {
    static_assert(sizeof...(Definitions) <= idComponentCapacity,
                  "Command table exceeds the 16-bit local position space");
public:
    using RootTypes = detail::TypeList<typename Definitions::Request...>;
    static constexpr std::size_t staticSize = sizeof...(Definitions);

    constexpr explicit CommandTable(Definitions... definitions) noexcept
        : definitions_(definitions...), entries_(makeEntries(std::index_sequence_for<Definitions...>{}))
    {}

    CommandTable(const CommandTable&) = delete;
    CommandTable& operator=(const CommandTable&) = delete;
    CommandTable(CommandTable&&) = delete;
    CommandTable& operator=(CommandTable&&) = delete;

    [[nodiscard]] constexpr const CommandEntry* data() const& noexcept { return entries_.data(); }
    const CommandEntry* data() const&& = delete;
    [[nodiscard]] constexpr std::size_t size() const noexcept { return staticSize; }

    // Erased iteration borrows this table; typed traversal below borrows its
    // exact definitions. Neither interface stores or materializes a value.
    [[nodiscard]] constexpr bool empty() const noexcept { return staticSize == 0; }
    [[nodiscard]] constexpr const CommandEntry* begin() const& noexcept { return entries_.data(); }
    const CommandEntry* begin() const&& = delete;
    [[nodiscard]] constexpr const CommandEntry* end() const& noexcept
    {
        // std::array may expose checked iterators on other standard libraries.
        // Our view uses pointers and never adds zero to a possibly null data().
        if constexpr (staticSize == 0) return entries_.data();
        else return entries_.data() + staticSize;
    }
    const CommandEntry* end() const&& = delete;
    [[nodiscard]] constexpr const CommandEntry& operator[](std::size_t i) const& noexcept { return entries_[i]; }
    const CommandEntry& operator[](std::size_t) const&& = delete;

    template <auto Position>
    [[nodiscard]] constexpr decltype(auto) get() const& noexcept
    {
        constexpr auto i = telemetry::detail::positionValue<Position>();
        static_assert(i < staticSize, "Command position is outside this table");
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
    [[nodiscard]] telemetry::CommandResult call(Args&&... args) const noexcept
    {
        constexpr auto i = telemetry::detail::positionValue<Position>();
        static_assert(i < staticSize, "Command position is outside this table");
        if constexpr (i < staticSize)
            return std::get<static_cast<std::size_t>(i)>(definitions_).call(std::forward<Args>(args)...);
    }

    template <class Registry>
    struct TypeStorage {
        inline static constexpr std::array<TypeId, staticSize> entries{{
            Registry::template typeId<typename Definitions::Request>()...}};
    };

private:
    template <class Definition>
    static constexpr std::uint32_t requiredScratch = [] {
        if constexpr (std::is_void_v<typename Definition::Request>) return std::uint32_t{0};
        else if constexpr (detail::localObject<typename Definition::Request>) return std::uint32_t{0};
        else {
            static_assert(scratchBytes<typename Definition::Request> <= UINT32_MAX,
                          "Command scratch size overflows u32");
            return static_cast<std::uint32_t>(scratchBytes<typename Definition::Request>);
        }
    }();

    template <class Definition, class... Args>
    static EncodedCommandResult invokeReady(const void* context, Args&&... args) noexcept
    {
        using Binding = typename Definition::BindingType;
        auto selected = detail::ErasedBinding<Binding>::snapshot(context);
        if (!Binding::available(selected)) return {DispatchStatus::Unavailable};
        const auto result = Binding::invoke(selected, std::forward<Args>(args)...);
        if (!model_detail::validStatus(result)) return {DispatchStatus::InternalError};
        return {DispatchStatus::Ok, result};
    }

    template <class Definition>
    static EncodedCommandResult invokeOne(const void* raw, const std::byte* bytes,
                                           Workspace& workspace) noexcept
    {
        using Request = typename Definition::Request;
        const std::span<const std::byte> input{bytes, wireSize<Request>};
        if constexpr (std::is_void_v<Request>) {
            return invokeReady<Definition>(raw);
        } else if constexpr (detail::localObject<Request>) {
            if (!detail::validEndpoint<Request>(input)) return {DispatchStatus::InvalidPayload};
            const Request request = detail::decodeLocalEndpoint<Request>(input);
            return invokeReady<Definition>(raw, request);
        } else {
            auto lease = workspace.reserve<Request>();
            if (!lease.valid()) return {DispatchStatus::WorkspaceTooSmall};
            Request* request = detail::decodeEndpoint<Request>(input, lease);
            if (request == nullptr) return {DispatchStatus::InvalidPayload};
            return invokeReady<Definition>(raw, *request);
        }
    }

    template <std::size_t... I>
    constexpr auto makeEntries(std::index_sequence<I...>) noexcept
    {
        return std::array<CommandEntry, staticSize>{{CommandEntry{
            detail::ErasedBinding<typename Definitions::BindingType>::context(std::get<I>(definitions_).binding_),
            &invokeOne<Definitions>, std::get<I>(definitions_).name(),
            wireSize<typename Definitions::Request>, requiredScratch<Definitions>}...}};
    }

    std::tuple<Definitions...> definitions_;
    std::array<CommandEntry, staticSize> entries_;
};

template <class... Definitions>
CommandTable(Definitions...) -> CommandTable<Definitions...>;

} // namespace telemetry

#endif
