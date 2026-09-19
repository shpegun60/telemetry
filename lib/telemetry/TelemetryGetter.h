/**
 * @file TelemetryGetter.h
 * @brief Non-owning noexcept getters for Scalar and native numeric sources.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 */
#ifndef TELEMETRY_GETTER_H
#define TELEMETRY_GETTER_H

#include <type_traits>
#include <utility>
#include <variant>

#include "tiny_delegate.hpp"
#include "TelemetryCompiler.h"
#include "TelemetryScalar.h"

namespace telemetry {

// Only noexcept targets are admitted; an empty getter returns Null. Bound
// methods/functions use tiny::delegate_ref. Native-returning function pointers
// retain their actual types so both bare/+ lambdas remain constexpr in C++17.
// No binding owns a source object or allocates storage.
class Getter {
    using Delegate = tiny::delegate_ref<Scalar()>;
    template <class R> using NativeFunction = R (*)() noexcept;
    using Storage = std::variant<Delegate,
        NativeFunction<bool>, NativeFunction<char>,
        NativeFunction<signed char>, NativeFunction<unsigned char>,
        NativeFunction<short>, NativeFunction<unsigned short>,
        NativeFunction<int>, NativeFunction<unsigned int>,
        NativeFunction<long>, NativeFunction<unsigned long>,
        NativeFunction<long long>, NativeFunction<unsigned long long>,
        NativeFunction<float>, NativeFunction<double>,
        NativeFunction<wchar_t>, NativeFunction<char16_t>, NativeFunction<char32_t>
#ifdef __cpp_char8_t
        , NativeFunction<char8_t>
#endif
        >;

public:
    using Function = Scalar (*)() noexcept;

    constexpr Getter(Function function = nullptr) noexcept
        : storage_(function != nullptr ? Delegate(function) : Delegate{}) {}

    template <class R, std::enable_if_t<std::is_constructible_v<Storage,
              std::in_place_type_t<NativeFunction<R>>, NativeFunction<R>>, int> = 0>
    constexpr Getter(NativeFunction<R> function) noexcept
        : storage_(std::in_place_type<NativeFunction<R>>, function) {}

    // Accept bare captureless lambdas in table rows. Convert implicitly so an
    // explicit conversion cannot override the one admitted by the constraint.
    template <class F, std::enable_if_t<std::is_class_v<std::decay_t<F>>
              && !std::is_base_of_v<Getter, std::decay_t<F>>
              && std::is_convertible_v<F&&, Function>, int> = 0>
    constexpr Getter(F&& function)
        noexcept(noexcept(as_function_(std::forward<F>(function))))
        : Getter(as_function_(std::forward<F>(function))) {}

    template <class F, class Native = decltype(+std::declval<F>()),
              std::enable_if_t<std::is_class_v<std::decay_t<F>>
              && !std::is_base_of_v<Getter, std::decay_t<F>>
              && !std::is_convertible_v<F&&, Function>
              && std::is_pointer_v<Native>
              && std::is_convertible_v<F&&, Native>
              && std::is_constructible_v<Storage, std::in_place_type_t<Native>, Native>, int> = 0>
    constexpr Getter(F&& function)
        noexcept(noexcept(as_native_<Native>(std::forward<F>(function))))
        : Getter(as_native_<Native>(std::forward<F>(function))) {}

    TELEMETRY_FORCE_INLINE Scalar operator()() const noexcept
    {
        return invokeStorage_(storage_, std::make_index_sequence<std::variant_size_v<Storage>>{});
    }

    constexpr explicit operator bool() const noexcept
    {
        return std::visit([](const auto& target) constexpr noexcept {
            return static_cast<bool>(target);
        }, storage_);
    }

    // Compatibility with the named free functions in existing tables.
    template <auto FunctionPointer>
    static constexpr Getter bind() noexcept
    {
        static_assert(std::is_pointer_v<decltype(FunctionPointer)>
                      && std::is_function_v<std::remove_pointer_t<decltype(FunctionPointer)>>,
                      "Getter::bind requires a function pointer");
        static_assert(std::is_nothrow_invocable_r_v<Scalar, decltype(FunctionPointer)>,
                      "The getter must return a Scalar-compatible value and be noexcept");
        return Getter(Delegate::bind<FunctionPointer>());
    }

    // The object must stay alive at the same address for every invocation.
    // A const object is supported when Method can be called on const T.
    template <auto Method, class T>
    static constexpr Getter bind(T& object) noexcept
    {
        static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                      "Getter::bind requires a member function");
        static_assert(std::is_nothrow_invocable_r_v<Scalar, decltype(Method), T&>,
                      "The getter must return a Scalar-compatible value and be noexcept");
        return Getter(Delegate::bind<Method>(object));
    }

private:
    template <std::size_t Index>
    TELEMETRY_FORCE_INLINE static Scalar invokeAlternative_(const Storage& storage) noexcept
    {
        const auto& target = std::get<Index>(storage);
        if constexpr (Index == 0) {
            return target.call_or([]() noexcept { return Scalar::null(); });
        } else {
            return target != nullptr ? Scalar::from(target()) : Scalar::null();
        }
    }

    template <std::size_t... Indices>
    TELEMETRY_FORCE_INLINE static Scalar invokeStorage_(const Storage& storage, std::index_sequence<Indices...>) noexcept
    {
        using Invoke = Scalar (*)(const Storage&) noexcept;
        static constexpr Invoke invokers[] = {&invokeAlternative_<Indices>...};
        // Storage cannot become valueless: all alternatives and assignments
        // are trivial and nothrow. Keep this selection at the call site so a
        // known alternative folds even in size-optimized builds.
        return invokers[storage.index()](storage);
    }

    static constexpr Function as_function_(Function function) noexcept { return function; }
    template <class Native>
    static constexpr Native as_native_(Native function) noexcept { return function; }

    constexpr explicit Getter(Delegate delegate) noexcept : storage_(delegate) {}

    static_assert(std::is_nothrow_copy_constructible_v<Storage>
                  && std::is_nothrow_move_constructible_v<Storage>
                  && std::is_nothrow_copy_assignable_v<Storage>
                  && std::is_nothrow_move_assignable_v<Storage>,
                  "Getter storage must not become valueless during assignment");
    Storage storage_;
};

static_assert(std::is_trivially_copyable_v<Getter>,
              "Catalog getters must remain trivial non-owning values");

} // namespace telemetry

#endif
