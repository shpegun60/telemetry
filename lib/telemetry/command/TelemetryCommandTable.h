/**
 * @file TelemetryCommandTable.h
 * @brief Owning compile-time command metadata with stable Command views.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_TABLE_H
#define TELEMETRY_COMMAND_TABLE_H

#include "TelemetryCommandFactory.h"
#include "TelemetryCommandCatalog.h"
#include "TelemetryCommandIndex.h"
#include <array>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry {
template <class... Definitions>
class CommandTable {
    static_assert((detail::IsOwnedCommandDefinition<Definitions>::value && ...),
                  "CommandTable accepts only command(...) definitions");
    static_assert(sizeof...(Definitions) <= idComponentCapacity,
                  "CommandTable exceeds the 16-bit entry capacity");
    using Metadata = std::tuple<typename Definitions::MetadataType...>;
    using DefinitionTuple = std::tuple<Definitions...>;

    // Initialization order matters: descriptors below point into metadata_.
    // Definitions survive only as types, retaining targets/signatures for the
    // native path without another runtime array of construction recipes.
    Metadata metadata_;
    std::array<Command, sizeof...(Definitions)> commands_;

    template <std::size_t I>
    constexpr auto metadataAddress_() const noexcept
    {
        using Value = std::tuple_element_t<I, Metadata>;
        if constexpr (std::is_same_v<Value, detail::NoCommandArgs>)
            return static_cast<const Value*>(nullptr);
        else
            return std::addressof(std::get<I>(metadata_));
    }

    template <std::size_t I, class... Input>
    TELEMETRY_FORCE_INLINE CommandResult callPosition_(Input... values) const noexcept
    {
        using Definition = std::tuple_element_t<I, DefinitionTuple>;
        return Definition::invokeTyped(commands_[I].owner, metadataAddress_<I>(), values...);
    }

    template <std::size_t I, class... Input>
    TELEMETRY_FORCE_INLINE bool callRuntimeMatch_(
        std::size_t index, CommandResult& result, Input... values) const noexcept
    {
        using Definition = std::tuple_element_t<I, DefinitionTuple>;
        if constexpr (Definition::template acceptsArguments<Input...>) {
            if (index == I) {
                result = callPosition_<I>(values...);
                return true;
            }
        }
        return false;
    }

    template <std::size_t... I, class... Input>
    TELEMETRY_FORCE_INLINE CommandResult callRuntime_(
        std::size_t index, std::index_sequence<I...>, Input... values) const noexcept
    {
        if (index >= sizeof...(Definitions)) return CommandResult::NotFound;
        CommandResult result = CommandResult::ArgumentCountMismatch;
        // Only matching arities emit branches. The selected definition knows
        // every target type and performs checked native conversions if needed.
        const bool matched = (callRuntimeMatch_<I>(index, result, values...) || ...);
        (void) matched;
        return result;
    }

    template <std::size_t... I>
    constexpr CommandTable(std::tuple<Definitions...> definitions,
                           std::index_sequence<I...>) noexcept
        : metadata_(std::get<I>(definitions).metadata...),
          commands_{std::get<I>(definitions).materialize(metadataAddress_<I>())...}
    {
        (void) definitions;
    }

public:
    using Descriptor = Command;
    constexpr explicit CommandTable(Definitions... definitions) noexcept
        : CommandTable(std::tuple<Definitions...>{definitions...},
                       std::index_sequence_for<Definitions...>{})
    {}

    // Moving or copying would leave descriptor metadata pointers referring to
    // the old object. Construct the table directly at its final address.
    CommandTable(const CommandTable&) = delete;
    CommandTable(CommandTable&&) = delete;
    CommandTable& operator=(const CommandTable&) = delete;
    CommandTable& operator=(CommandTable&&) = delete;

    constexpr const Command* data() const & noexcept { return commands_.data(); }
    const Command* data() const && = delete;
    constexpr std::size_t size() const noexcept { return commands_.size(); }
    constexpr bool empty() const noexcept { return commands_.empty(); }
    constexpr auto begin() const & noexcept { return commands_.begin(); }
    constexpr auto end() const & noexcept { return commands_.end(); }
    auto begin() const && = delete;
    auto end() const && = delete;
    // Raw descriptor access follows std::array's unchecked indexing contract.
    // Use index().find(...) for a checked runtime position.
    constexpr const Command& operator[](std::size_t index) const & noexcept
    { return commands_[index]; }
    const Command& operator[](std::size_t) const && = delete;
    constexpr CommandIndex index() const & noexcept
    { return CommandIndex{commands_.data(), commands_.size()}; }
    CommandIndex index() const && = delete;

    // Position is a zero-based integer or enum value in this owning table.
    // The compile-time form resolves the target; runtime emits typed branches.
    // Neither path constructs Scalar values or calls Command::Invoke.
    template <auto Position, class... Input>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult call(Input... values) const noexcept
    {
        constexpr auto Index = detail::positionValue<Position>();
        constexpr bool native = (detail::isFactoryValue<Input> && ...);
        static_assert(Index < sizeof...(Definitions),
                      "Typed command index is outside CommandTable");
        static_assert(native,
                      "Typed CommandTable calls require native numeric or enum values");
        if constexpr (Index < sizeof...(Definitions)) {
            using Definition = std::tuple_element_t<Index, DefinitionTuple>;
            static_assert(Definition::reserved || sizeof...(Input) == Definition::arity,
                          "Typed command argument count must match the selected target");
            if constexpr (Definition::reserved || sizeof...(Input) == Definition::arity) {
                if constexpr (native)
                    return callPosition_<Index>(values...);
            }
        }
        return CommandResult::ArgumentCountMismatch;
    }

    template <class... Input>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult call(std::size_t runtimeIndex, Input... values) const noexcept
    {
        constexpr bool native = (detail::isFactoryValue<Input> && ...);
        static_assert(native,
                      "Typed CommandTable calls require native numeric or enum values");
        if constexpr (native)
            return callRuntime_(runtimeIndex,
                                std::index_sequence_for<Definitions...>{}, values...);
        return CommandResult::InvalidValue;
    }
};

template <class... Definitions>
CommandTable(Definitions...) -> CommandTable<std::decay_t<Definitions>...>;

} // namespace telemetry
#endif
