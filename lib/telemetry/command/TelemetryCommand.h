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
#include <functional>
#include <memory>
#include <type_traits>

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

// One immutable table per signature/metadata type, shared by all owners.
// A real zero-argument command has a table with count == 0; a reserved
// descriptor has no table. No parameter storage is added to Command itself.
struct CommandParamOps {
    std::uint32_t count;
    bool (*forEach)(const void*, void*, CommandParamSink) noexcept;
    bool (*at)(const void*, std::uint32_t, void*, CommandParamSink) noexcept;
};

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

    const char* const name = "";
    // Public const storage keeps the descriptor standard-layout for ABI checks.
    const void* const owner = nullptr;
    const void* const metadata = nullptr;
    const Invoke invoke = nullptr;
    const CommandParamOps* const params = nullptr;

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

    constexpr bool hasDescription() const noexcept { return detail::pointerPresent(params); }
    constexpr std::uint32_t parameterCount() const noexcept { return hasDescription() ? params->count : 0; }

    bool describeParameter(std::uint32_t index, void* context, CommandParamSink sink) const noexcept
    {
        return sink != nullptr && index < parameterCount() && params->at(metadata, index, context, sink);
    }

    TELEMETRY_FORCE_INLINE bool describeParameters(void* context, CommandParamSink sink) const noexcept
    {
        return hasDescription() && sink != nullptr && params->forEach(metadata, context, sink);
    }

    // Synchronous adapter: no parameter array, visitor copy or retained context.
    // Returning false stops traversal. Parameter references last only until the
    // visitor returns; copy a description if it is needed after that call.
    template <class Visitor>
    bool forEachParameter(Visitor&& visitor) const noexcept
    {
        return visit_<false>(0, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    bool visitParameter(std::uint32_t index, Visitor&& visitor) const noexcept
    {
        return visit_<true>(index, std::forward<Visitor>(visitor));
    }

private:
    template <bool Indexed, class Visitor>
    bool visit_(std::uint32_t index, Visitor&& visitor) const noexcept
    {
        static_assert(std::is_nothrow_invocable_r_v<bool, Visitor&, const CommandParam&>,
                      "Command parameter visitor must be noexcept and return bool");
        if constexpr (std::is_pointer_v<std::remove_reference_t<Visitor>>) {
            if (visitor == nullptr) return false;
        }
        using Callable = std::remove_reference_t<Visitor>;
        if constexpr (std::is_function_v<Callable>) {
            // A function is not an object and cannot be sent through void*.
            // Its pointer is an object; borrow that local pointer synchronously.
            return visit_<Indexed>(index, std::addressof(visitor));
        } else {
            // Pass the callable itself, without a second pointer-holding context.
            // Restore its exact cv-qualified type in the thunk before invocation;
            // the erased mutable pointer does not permit mutating a const visitor.
            void* context = const_cast<void*>(static_cast<const volatile void*>(std::addressof(visitor)));
            const auto sink = +[](void* raw, const CommandParam& parameter) noexcept {
                return static_cast<bool>(std::invoke(*static_cast<Callable*>(raw), parameter));
            };
            if constexpr (Indexed) return describeParameter(index, context, sink);
            else return describeParameters(context, sink);
        }
    }

private:
    template <auto, class, class> friend struct detail::CommandBinding;
    template <class, class> friend struct detail::BorrowedCommandBinding;
    constexpr Command(const char* label, const void* object,
                       const void* parameters, Invoke run, const CommandParamOps* schema) noexcept
        : name(label), owner(object), metadata(parameters),
          invoke(run), params(schema) {}
};
static_assert(std::is_standard_layout_v<Command> && std::is_trivially_copyable_v<Command>);
} // namespace telemetry
#endif
