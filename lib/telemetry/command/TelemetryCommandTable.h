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
namespace detail {

template <class... Entries>
constexpr auto ownedCommandMetadata(Entries... entries) noexcept
{
    static_assert((isIndexedArgumentMetadata<Entries> && ...),
                  "command(...) metadata must use indexed arg<N>(...)");
    if constexpr (sizeof...(Entries) == 0) return NoCommandArgs{};
    else return CommandArgs<std::decay_t<Entries>...>{
        std::tuple<std::decay_t<Entries>...>{entries...}};
}

template <auto Target, class Owner, class Metadata>
struct OwnedCommandDefinition {
    using MetadataType = Metadata;
    using Binding = CommandBinding<Target, Owner, Metadata>;
    static constexpr std::size_t arity = Binding::Traits::arity;
    template <class... Input>
    static constexpr bool signatureMatches = std::is_same_v<
        std::tuple<std::decay_t<Input>...>, typename Binding::Traits::Arguments>;
    CommandId id;
    const char* name;
    Owner* owner;
    Metadata metadata;

    constexpr Command materialize(const Metadata* stored) const noexcept
    {
        if constexpr (std::is_same_v<Metadata, NoCommandArgs>)
            return CommandBinding<Target, Owner>::make(id, name, owner);
        else
            return CommandBinding<Target, Owner, Metadata>::make(id, name, owner, stored);
    }

    template <class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult invokeTyped(
        const void* target, const Metadata* stored, Input... values) noexcept
    {
        if constexpr (!signatureMatches<Input...>)
            return CommandResult::ArgumentCountMismatch;
        else
            return Binding::callNative(target, stored, values...);
    }
};

template <class Callable, class Metadata>
struct OwnedBorrowedCommandDefinition {
    using MetadataType = Metadata;
    using Binding = BorrowedCommandBinding<Callable, Metadata>;
    static constexpr std::size_t arity = Binding::Traits::arity;
    template <class... Input>
    static constexpr bool signatureMatches = std::is_same_v<
        std::tuple<std::decay_t<Input>...>, typename Binding::Traits::Arguments>;
    CommandId id;
    const char* name;
    Callable* callable;
    Metadata metadata;

    constexpr Command materialize(const Metadata* stored) const noexcept
    {
        if constexpr (std::is_same_v<Metadata, NoCommandArgs>)
            return BorrowedCommandBinding<Callable>::make(id, name, callable);
        else
            return BorrowedCommandBinding<Callable, Metadata>::make(
                id, name, callable, stored);
    }

    template <class... Input>
    TELEMETRY_FORCE_INLINE static CommandResult invokeTyped(
        const void* target, const Metadata* stored, Input... values) noexcept
    {
        if constexpr (!signatureMatches<Input...>)
            return CommandResult::ArgumentCountMismatch;
        else
            return Binding::callNative(target, stored, values...);
    }
};

template <class T> struct IsOwnedCommandDefinition : std::false_type {};
template <auto Target, class Owner, class Metadata>
struct IsOwnedCommandDefinition<OwnedCommandDefinition<Target, Owner, Metadata>>
    : std::true_type {};
template <class Callable, class Metadata>
struct IsOwnedCommandDefinition<OwnedBorrowedCommandDefinition<Callable, Metadata>>
    : std::true_type {};
} // namespace detail

// High-level definitions own their metadata values. Owners, callable objects
// and strings remain borrowed. Direct CommandTable construction gives metadata
// a stable address and emits ordinary Command descriptors without changing ABI.
template <auto Target, class Owner, class... Entries,
          std::enable_if_t<std::is_member_function_pointer_v<decltype(Target)>
              && std::is_lvalue_reference_v<Owner&&>, int> = 0>
constexpr auto command(CommandId id, const char* name, Owner&& owner,
                       Entries... entries) noexcept
{
    auto metadata = detail::ownedCommandMetadata(entries...);
    using StoredOwner = std::remove_reference_t<Owner>;
    using Metadata = decltype(metadata);
    return detail::OwnedCommandDefinition<Target, StoredOwner, Metadata>{
        id, name, std::addressof(owner), metadata};
}

template <auto Target, class... Entries,
          std::enable_if_t<!std::is_member_function_pointer_v<decltype(Target)>, int> = 0>
constexpr auto command(CommandId id, const char* name, Entries... entries) noexcept
{
    auto metadata = detail::ownedCommandMetadata(entries...);
    using Metadata = decltype(metadata);
    return detail::OwnedCommandDefinition<Target, detail::NoOwner, Metadata>{
        id, name, nullptr, metadata};
}

template <class Callable, class... Entries,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Callable>>, int> = 0>
constexpr auto command(CommandId id, const char* name, Callable& callable,
                       Entries... entries) noexcept
{
    static_assert(detail::HasConcreteCallOperator<Callable>::value,
                  "Borrowed command callable must have one concrete operator(); generic and overloaded callables are unsupported");
    auto metadata = detail::ownedCommandMetadata(entries...);
    using Metadata = decltype(metadata);
    return detail::OwnedBorrowedCommandDefinition<Callable, Metadata>{
        id, name, std::addressof(callable), metadata};
}

template <class Callable, class... Entries,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Callable>>
              && !std::is_lvalue_reference_v<Callable>, int> = 0>
auto command(CommandId, const char*, Callable&&, Entries...) = delete;

template <class... Definitions>
class CommandTable {
    static_assert((detail::IsOwnedCommandDefinition<Definitions>::value && ...),
                  "CommandTable accepts only command(...) definitions");
    using Metadata = std::tuple<typename Definitions::MetadataType...>;
    using DefinitionTuple = std::tuple<Definitions...>;

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
        if constexpr (Definition::template signatureMatches<Input...>) {
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
        // Each helper emits code only when Definition I has this exact native
        // signature. Valid positions with another signature keep the mismatch
        // result without adding a comparison or invocation for that row.
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
    constexpr explicit CommandTable(Definitions... definitions) noexcept
        : CommandTable(std::tuple<Definitions...>{definitions...},
                       std::index_sequence_for<Definitions...>{})
    {}

    CommandTable(const CommandTable&) = delete;
    CommandTable(CommandTable&&) = delete;
    CommandTable& operator=(const CommandTable&) = delete;
    CommandTable& operator=(CommandTable&&) = delete;

    constexpr const Command* data() const & noexcept { return commands_.data(); }
    const Command* data() const && = delete;
    constexpr std::size_t size() const noexcept { return commands_.size(); }
    constexpr const Command& operator[](std::size_t index) const & noexcept
    { return commands_[index]; }
    const Command& operator[](std::size_t) const && = delete;
    constexpr CommandIndex index() const & noexcept
    { return CommandIndex{commands_.data(), commands_.size()}; }
    CommandIndex index() const && = delete;

    // Index is the zero-based position in this owning table. The compile-time
    // form resolves the concrete target; the runtime form emits typed branches.
    // Neither path constructs Scalar values or calls Command::Invoke.
    template <std::size_t Index, class... Input>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult call(Input... values) const noexcept
    {
        constexpr bool native = (detail::isFactoryValue<Input> && ...);
        static_assert(Index < sizeof...(Definitions),
                      "Typed command index is outside CommandTable");
        static_assert(native,
                      "Typed CommandTable calls require native numeric or enum values");
        if constexpr (Index < sizeof...(Definitions)) {
            using Definition = std::tuple_element_t<Index, DefinitionTuple>;
            static_assert(sizeof...(Input) == Definition::arity,
                          "Typed command argument count must match the selected target");
            if constexpr (sizeof...(Input) == Definition::arity) {
                constexpr bool signature = Definition::template signatureMatches<Input...>;
                static_assert(!native || signature,
                              "Typed command values must exactly match the selected target signature");
                if constexpr (native && signature)
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

// One owning group. The resulting catalog() is the ordinary runtime view used
// by CommandCatalogIndex; lookup never depends on this template builder.
template <class... Definitions>
class CommandCatalogTable {
    const GroupId id_;
    const char* const name_;
    CommandTable<Definitions...> commands_;

public:
    constexpr CommandCatalogTable(GroupId id, const char* name,
                                  Definitions... definitions) noexcept
        : id_(id), name_(name), commands_(definitions...)
    {}

    CommandCatalogTable(const CommandCatalogTable&) = delete;
    CommandCatalogTable(CommandCatalogTable&&) = delete;
    CommandCatalogTable& operator=(const CommandCatalogTable&) = delete;
    CommandCatalogTable& operator=(CommandCatalogTable&&) = delete;

    constexpr CommandCatalog catalog() const & noexcept
    { return CommandCatalog{id_, name_, commands_.data(), commands_.size()}; }
    CommandCatalog catalog() const && = delete;
    constexpr const Command* data() const & noexcept { return commands_.data(); }
    const Command* data() const && = delete;
    constexpr std::size_t size() const noexcept { return commands_.size(); }

    template <std::size_t Index, class... Input>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult call(Input... values) const noexcept
    { return commands_.template call<Index>(values...); }

    template <class... Input>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult call(std::size_t runtimeIndex, Input... values) const noexcept
    { return commands_.call(runtimeIndex, values...); }
};

template <class... Definitions>
CommandCatalogTable(GroupId, const char*, Definitions...)
    -> CommandCatalogTable<std::decay_t<Definitions>...>;
} // namespace telemetry
#endif
