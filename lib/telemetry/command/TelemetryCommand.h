/**
 * @file TelemetryCommand.h
 * @brief Non-owning commands with checked positional arguments and no allocation.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMMAND_H
#define TELEMETRY_COMMAND_H

#include "../core/TelemetryId.h"
#include "../field/TelemetryFieldType.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace telemetry {
struct CommandParam {
    std::size_t index = 0;
    const char* name = nullptr;
    const char* unit = nullptr;
    FieldType type{};
};
// Descriptions are produced on demand; neither the command nor the sink owns
// a persistent CommandParam array. Copy a parameter if it must survive the call.
// The parameter reference is valid only for this synchronous sink invocation.
using CommandParamSink = bool (*)(void*, const CommandParam&) noexcept;

enum class CommandResult : std::uint8_t {
    Executed = 0,
    Accepted, // Queued by the owner; completion has not been established.
    NotFound,
    Unavailable,
    ArgumentCountMismatch,
    InvalidValue,
    Busy,
    Failed,
};

namespace detail {
template <auto, class, class> struct CommandBinding;
template <class, class> struct BorrowedCommandBinding;
}

// Definitions borrow the owner, metadata and strings at stable addresses.
// execute validates all arguments before invoking the owner exactly once.
// No defaults are filled in: they describe initial UI values, not omitted args.
struct Command {
    using Invoke = CommandResult (*)(const void*, const void*, const Scalar*, std::size_t) noexcept;
    using Describe = bool (*)(const void*, void*, CommandParamSink) noexcept;

    const char* const name = "";
    // Public const storage keeps the descriptor standard-layout for ABI checks.
    const void* const owner = nullptr;
    const void* const metadata = nullptr;
    const Invoke invoke = nullptr;
    const Describe describe = nullptr;

    constexpr Command() noexcept = default;

    [[nodiscard]] TELEMETRY_FORCE_INLINE
    CommandResult execute(const Scalar* values, std::size_t count) const noexcept
    {
        if (invoke == nullptr) return CommandResult::Unavailable;
        return invoke(owner, metadata, values, count);
    }

    template <class... A, std::enable_if_t<((detail::isFactoryValue<A>
              || std::is_same_v<A, Scalar>) && ...), int> = 0>
    [[nodiscard]] TELEMETRY_FORCE_INLINE CommandResult call(A... values) const noexcept
    {
        // This descriptor is type-erased. Native direct calls live on
        // CommandTable; this convenience path deliberately normalizes Scalars.
        const std::array<Scalar, sizeof...(A)> args{detail::factoryScalar(values)...};
        return execute(args.data(), args.size());
    }

    bool describeParameters(void* context, CommandParamSink sink) const noexcept
    {
        return describe != nullptr && sink != nullptr && describe(metadata, context, sink);
    }

private:
    template <auto, class, class> friend struct detail::CommandBinding;
    template <class, class> friend struct detail::BorrowedCommandBinding;
    constexpr Command(const char* label, const void* object,
                       const void* parameters, Invoke run, Describe schema) noexcept
        : name(label), owner(object), metadata(parameters),
          invoke(run), describe(schema) {}
};
static_assert(std::is_standard_layout_v<Command> && std::is_trivially_copyable_v<Command>);
} // namespace telemetry
#endif
