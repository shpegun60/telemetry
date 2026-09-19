/**
 * @file TelemetrySetter.h
 * @brief Optional non-owning write callbacks and synchronous write results.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_SETTER_H
#define TELEMETRY_SETTER_H

#include <type_traits>
#include <utility>

#include "tiny_delegate.hpp"
#include "../core/TelemetryCompiler.h"
#include "../core/TelemetryScalar.h"

namespace telemetry {

// Applied means the owner has applied the value before returning. Persistence
// and multi-field transactions are separate owner operations.
enum class WriteResult : std::uint8_t {
    Applied = 0,
    NotFound,
    ReadOnly,
    InvalidValue,
    Busy,
};

// Non-owning, noexcept write callback. Empty means ReadOnly. The argument is
// borrowed only for this call; an owner retaining its value must copy it.
// Field::write / CatalogIndex::write convert to the declared ScalarType before
// dispatch. Calling a populated Setter directly leaves validation to its owner.
class Setter {
    using Delegate = tiny::delegate_ref<WriteResult(const Scalar&)>;

public:
    using Function = WriteResult (*)(const Scalar&) noexcept;

    constexpr Setter(Function function = nullptr) noexcept
        : delegate_(function != nullptr ? Delegate(function) : Delegate{}) {}

    template <class F, std::enable_if_t<std::is_class_v<std::decay_t<F>>
              && !std::is_base_of_v<Setter, std::decay_t<F>>
              && std::is_convertible_v<F&&, Function>, int> = 0>
    constexpr Setter(F&& function)
        noexcept(noexcept(as_function_(std::forward<F>(function))))
        : Setter(as_function_(std::forward<F>(function))) {}

    [[nodiscard]] TELEMETRY_FORCE_INLINE WriteResult operator()(const Scalar& value) const noexcept
    {
        return delegate_.call_or([](const Scalar&) noexcept { return WriteResult::ReadOnly; }, value);
    }

    constexpr explicit operator bool() const noexcept
    {
        return static_cast<bool>(delegate_);
    }

    template <auto FunctionPointer>
    static constexpr Setter bind() noexcept
    {
        static_assert(std::is_convertible_v<decltype(FunctionPointer), Function>,
                      "The setter must return WriteResult and be noexcept");
        return Setter(Delegate::bind<FunctionPointer>());
    }

    // The object must remain alive at the same address for every invocation.
    // Const objects are supported when Method is callable on const T.
    // Let T be deduced; explicit reference types must not hide a temporary.
    template <auto Method, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static constexpr Setter bind(T& object) noexcept
    {
        static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                      "Setter::bind requires a member function");
        static_assert(std::is_nothrow_invocable_r_v<WriteResult, decltype(Method), T&, const Scalar&>,
                      "The setter must accept const Scalar&, return WriteResult and be noexcept");
        return Setter(Delegate::bind<Method>(object));
    }

    // Also catches an explicit const T, whose T& could otherwise bind an rvalue.
    template <auto Method, class T>
    static Setter bind(T&&) = delete;

private:
    static constexpr Function as_function_(Function function) noexcept { return function; }

    constexpr explicit Setter(Delegate delegate) noexcept : delegate_(delegate) {}

    Delegate delegate_;
};

static_assert(std::is_trivially_copyable_v<Setter>,
              "Catalog setters must remain trivial non-owning values");
static_assert(sizeof(Setter) == sizeof(tiny::delegate_ref<WriteResult(const Scalar&)>),
              "Telemetry policy must not add delegate storage");

} // namespace telemetry

#endif
