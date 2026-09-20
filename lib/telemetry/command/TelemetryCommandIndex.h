/**
 * @file TelemetryCommandIndex.h
 * @brief Direct lookup in a borrowed dense command table with a separate ID space.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_INDEX_H
#define TELEMETRY_COMMAND_INDEX_H

#include "TelemetryCommand.h"
#include <limits>

namespace telemetry {
class CommandIndex {
public:
    constexpr CommandIndex() noexcept = default;
    template <class = void>
    constexpr CommandIndex(const Command* commands, std::size_t count) noexcept : commands_(commands)
    {
        if (commands == nullptr) return;
        while (count_ < count && count_ <= std::numeric_limits<CommandId>::max()
               && commands[count_].id == static_cast<CommandId>(count_)) ++count_;
    }
    template <std::size_t N>
    constexpr explicit CommandIndex(const Command (&commands)[N]) noexcept
        : CommandIndex(static_cast<const Command*>(commands), N) {}
    template <std::size_t N> CommandIndex(const Command (&&)[N]) = delete;
    template <std::size_t N> CommandIndex(const Command (&&)[N], std::size_t) = delete;

    TELEMETRY_FORCE_INLINE constexpr const Command* find(CommandId id) const noexcept
    {
        return id < count_ ? commands_ + id : nullptr;
    }
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult execute(CommandId id, const Scalar* values, std::size_t count) const noexcept
    {
        const Command* command = find(id);
        return command != nullptr ? command->execute(values, count) : CommandResult::NotFound;
    }
    template <class... A>
    [[nodiscard]] TELEMETRY_FORCE_INLINE auto call(CommandId id, A... values) const noexcept
        -> decltype(std::declval<const Command&>().call(values...))
    {
        const Command* command = find(id);
        return command != nullptr ? command->call(values...) : CommandResult::NotFound;
    }
    constexpr const Command* data() const noexcept { return commands_; }
    constexpr std::size_t size() const noexcept { return count_; }
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
