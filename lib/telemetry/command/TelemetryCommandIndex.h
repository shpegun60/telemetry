/**
 * @file TelemetryCommandIndex.h
 * @brief Direct lookup in a borrowed dense command table with a separate ID space.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_INDEX_H
#define TELEMETRY_COMMAND_INDEX_H

#include "TelemetryCommand.h"

namespace telemetry {
// This view borrows contiguous descriptors. The caller supplies their actual
// extent and keeps descriptors, owners and metadata alive for every invocation.
// IDs here are local positions; packed group IDs belong to CommandCatalogIndex.
class CommandIndex {
public:
    constexpr CommandIndex() noexcept = default;
    template <class = void>
    constexpr CommandIndex(const Command* commands, std::size_t count) noexcept
        : commands_(commands), count_(commands == nullptr ? 0
              : (count < idComponentCapacity ? count : idComponentCapacity)) {}
    template <std::size_t N>
    constexpr explicit CommandIndex(const Command (&commands)[N]) noexcept
        : CommandIndex(static_cast<const Command*>(commands), N) {}
    template <std::size_t N> CommandIndex(const Command (&&)[N]) = delete;
    template <std::size_t N> CommandIndex(const Command (&&)[N], std::size_t) = delete;

    TELEMETRY_FORCE_INLINE constexpr const Command* find(CommandId id) const noexcept
    {
        return id < count_ ? commands_ + id : nullptr;
    }

    template <class Id, std::enable_if_t<detail::isIdInput<Id>
        && !std::is_same_v<Id, CommandId>, int> = 0>
    TELEMETRY_FORCE_INLINE constexpr const Command* find(Id id) const noexcept
    {
        return detail::indexFits<CommandId>(id)
            ? find(static_cast<CommandId>(id)) : nullptr;
    }
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult execute(CommandId id, const Scalar* values, std::size_t count) const noexcept
    {
        const Command* command = find(id);
        return command != nullptr ? command->execute(values, count) : CommandResult::NotFound;
    }

    template <class Id, std::enable_if_t<detail::isIdInput<Id>
        && !std::is_same_v<Id, CommandId>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult execute(Id id, const Scalar* values, std::size_t count) const noexcept
    {
        const Command* command = find(id);
        return command != nullptr ? command->execute(values, count) : CommandResult::NotFound;
    }
    template <class... A>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto call(CommandId id, A... values) const noexcept
        -> decltype(std::declval<const Command&>().call(values...))
    {
        // The descriptor no longer carries its C++ definition type, so this
        // convenience overload uses checked Scalar conversion, not typed dispatch.
        const Command* command = find(id);
        return command != nullptr ? command->call(values...) : CommandResult::NotFound;
    }

    template <class Id, class... A, std::enable_if_t<detail::isIdInput<Id>
        && !std::is_same_v<Id, CommandId>, int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto call(Id id, A... values) const noexcept
        -> decltype(std::declval<const Command&>().call(values...))
    {
        const Command* command = find(id);
        return command != nullptr ? command->call(values...) : CommandResult::NotFound;
    }
    constexpr const Command* data() const noexcept { return commands_; }
    constexpr std::size_t size() const noexcept { return count_; }
    constexpr bool empty() const noexcept { return count_ == 0; }
    constexpr const Command* begin() const noexcept { return commands_; }
    constexpr const Command* end() const noexcept { return commands_ != nullptr ? commands_ + count_ : nullptr; }
    static constexpr std::size_t abiCommandsOffset() noexcept;
    static constexpr std::size_t abiCountOffset() noexcept;
private:
    const Command* commands_ = nullptr;
    std::size_t count_ = 0;
};

constexpr std::size_t CommandIndex::abiCommandsOffset() noexcept
{ return offsetof(CommandIndex, commands_); }
constexpr std::size_t CommandIndex::abiCountOffset() noexcept
{ return offsetof(CommandIndex, count_); }
} // namespace telemetry
#endif
