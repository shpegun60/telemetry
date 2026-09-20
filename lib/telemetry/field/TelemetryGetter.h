/**
 * @file TelemetryGetter.h
 * @brief Compact non-owning noexcept getters for Scalar and native numbers.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_GETTER_H
#define TELEMETRY_GETTER_H

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "../core/TelemetryCompiler.h"
#include "../core/TelemetryScalar.h"

namespace telemetry {
namespace detail { struct FieldTableAccess; }

// A getter is exactly one target-sized payload plus one generated invoker.
// Native function pointers retain their exact types; object bindings borrow a
// stable lvalue. No function-pointer type punning, allocation or ownership is
// involved. An empty getter returns Null.
class Getter {
    friend struct detail::FieldTableAccess;
    template <class T> using NativeFunction = T (*)() noexcept;
    template <class T> struct NativeTag {};

#define TELEMETRY_GETTER_NATIVE_TYPES(X) \
    X(bool, boolean)                     \
    X(char, character)                   \
    X(signed char, signedCharacter)      \
    X(unsigned char, unsignedCharacter)  \
    X(short, signedShort)                \
    X(unsigned short, unsignedShort)     \
    X(int, signedInt)                    \
    X(unsigned int, unsignedInt)         \
    X(long, signedLong)                  \
    X(unsigned long, unsignedLong)       \
    X(long long, signedLongLong)         \
    X(unsigned long long, unsignedLongLong) \
    X(float, float32)                    \
    X(double, float64)                   \
    X(wchar_t, wideCharacter)            \
    X(char16_t, character16)             \
    X(char32_t, character32)

    union Payload {
        void* object;
        Scalar (*scalar)() noexcept;
#define TELEMETRY_GETTER_MEMBER(Type, Name) NativeFunction<Type> Name;
        TELEMETRY_GETTER_NATIVE_TYPES(TELEMETRY_GETTER_MEMBER)
#undef TELEMETRY_GETTER_MEMBER
#ifdef __cpp_char8_t
        NativeFunction<char8_t> character8;
#endif

        constexpr Payload() noexcept : object(nullptr) {}
        constexpr explicit Payload(void* value) noexcept : object(value) {}
        constexpr explicit Payload(Scalar (*value)() noexcept) noexcept : scalar(value) {}
#define TELEMETRY_GETTER_CTOR(Type, Name) \
        constexpr Payload(NativeFunction<Type> value, NativeTag<Type>) noexcept : Name(value) {}
        TELEMETRY_GETTER_NATIVE_TYPES(TELEMETRY_GETTER_CTOR)
#undef TELEMETRY_GETTER_CTOR
#ifdef __cpp_char8_t
        constexpr Payload(NativeFunction<char8_t> value, NativeTag<char8_t>) noexcept
            : character8(value) {}
#endif
    };

    using Invoke = Scalar (*)(Payload) noexcept;

public:
    using Function = Scalar (*)() noexcept;

    constexpr Getter(Function function = nullptr) noexcept
        : payload_(function), invoke_(function != nullptr ? &invokeScalar_ : nullptr) {}

    template <class R, std::enable_if_t<detail::isScalarReadType<R>, int> = 0>
    constexpr Getter(NativeFunction<R> function) noexcept
        : payload_(function, NativeTag<R>{}),
          invoke_(function != nullptr ? &invokeNative_<R> : nullptr) {}

    // A capture-free lambda converts to its exact noexcept function pointer.
    template <class F, std::enable_if_t<std::is_class_v<std::decay_t<F>>
              && !std::is_base_of_v<Getter, std::decay_t<F>>
              && std::is_convertible_v<F&&, Function>, int> = 0>
    constexpr Getter(F&& function)
        noexcept(noexcept(asFunction_(std::forward<F>(function))))
        : Getter(asFunction_(std::forward<F>(function))) {}

    template <class F, class Native = decltype(+std::declval<F>()),
              std::enable_if_t<std::is_class_v<std::decay_t<F>>
              && !std::is_base_of_v<Getter, std::decay_t<F>>
              && !std::is_convertible_v<F&&, Function>
              && std::is_pointer_v<Native>
              && std::is_convertible_v<F&&, Native>
              && std::is_nothrow_invocable_v<Native>
              && detail::isScalarReadType<std::invoke_result_t<Native>>, int> = 0>
    constexpr Getter(F&& function)
        noexcept(noexcept(asNative_<Native>(std::forward<F>(function))))
        : Getter(asNative_<Native>(std::forward<F>(function))) {}

    [[nodiscard]] TELEMETRY_FORCE_INLINE Scalar operator()() const noexcept
    {
        return invoke_ != nullptr ? invoke_(payload_) : Scalar::null();
    }

    constexpr explicit operator bool() const noexcept { return invoke_ != nullptr; }

    // Exact private layout for the link-time ABI signature.
    static constexpr std::size_t abiPayloadOffset() noexcept
    { return offsetof(Getter, payload_); }
    static constexpr std::size_t abiInvokeOffset() noexcept
    { return offsetof(Getter, invoke_); }
    static constexpr std::size_t abiPayloadSize() noexcept { return sizeof(Payload); }
    static constexpr std::size_t abiPayloadAlign() noexcept { return alignof(Payload); }

    // Compile-time functions need no stored target address.
    template <auto FunctionPointer>
    static constexpr Getter bind() noexcept
    {
        static_assert(std::is_pointer_v<decltype(FunctionPointer)>
                      && std::is_function_v<std::remove_pointer_t<decltype(FunctionPointer)>>,
                      "Getter::bind requires a function pointer");
        static_assert(FunctionPointer != nullptr, "Getter target cannot be null");
        static_assert(std::is_nothrow_invocable_r_v<Scalar, decltype(FunctionPointer)>,
                      "The getter must return a Scalar-compatible value and be noexcept");
        return Getter(Payload{}, &invokeStatic_<FunctionPointer>);
    }

    // The object must remain alive at the same address for every invocation.
    template <auto Method, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static constexpr Getter bind(T& object) noexcept
    {
        static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                      "Getter::bind requires a member function");
        static_assert(!std::is_volatile_v<T>, "Getter owners cannot be volatile");
        static_assert(std::is_nothrow_invocable_r_v<Scalar, decltype(Method), T&>,
                      "The getter must return a Scalar-compatible value and be noexcept");
        return Getter(Payload(eraseObject_(std::addressof(object))), &invokeMethod_<Method, T>);
    }

    template <auto Method, class T>
    static Getter bind(T&&) = delete;

    template <auto Adapter, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static constexpr Getter bindContext(T& object) noexcept
    {
        static_assert(!std::is_volatile_v<T>, "Getter contexts cannot be volatile");
        static_assert(std::is_nothrow_invocable_r_v<Scalar, decltype(Adapter), T&>,
                      "Getter adapter must return a Scalar-compatible value and be noexcept");
        return Getter(Payload(eraseObject_(std::addressof(object))), &invokeContext_<Adapter, T>);
    }

    template <auto Adapter, class T>
    static Getter bindContext(T&&) = delete;

private:
    constexpr Getter(Payload payload, Invoke invoke) noexcept
        : payload_(payload), invoke_(invoke) {}

    static TELEMETRY_FORCE_INLINE Scalar invokeScalar_(Payload payload) noexcept
    {
        return payload.scalar();
    }

#define TELEMETRY_GETTER_ACCESSOR(Type, Name) \
    static constexpr NativeFunction<Type> native_(Payload payload, NativeTag<Type>) noexcept \
    { return payload.Name; }
    TELEMETRY_GETTER_NATIVE_TYPES(TELEMETRY_GETTER_ACCESSOR)
#undef TELEMETRY_GETTER_ACCESSOR
#ifdef __cpp_char8_t
    static constexpr NativeFunction<char8_t> native_(Payload payload, NativeTag<char8_t>) noexcept
    { return payload.character8; }
#endif

    template <class R>
    static TELEMETRY_FORCE_INLINE Scalar invokeNative_(Payload payload) noexcept
    {
        return Scalar::from(native_(payload, NativeTag<R>{})());
    }

    template <auto FunctionPointer>
    static TELEMETRY_FORCE_INLINE Scalar invokeStatic_(Payload) noexcept
    {
        return FunctionPointer();
    }

    template <class T>
    static TELEMETRY_FORCE_INLINE T& object_(Payload payload) noexcept
    {
        return *static_cast<T*>(payload.object);
    }

    template <class T>
    static constexpr void* eraseObject_(T* pointer) noexcept
    {
        return const_cast<void*>(static_cast<const volatile void*>(pointer));
    }

    template <auto Method, class T>
    static TELEMETRY_FORCE_INLINE Scalar invokeMethod_(Payload payload) noexcept
    {
        return std::invoke(Method, object_<T>(payload));
    }

    template <auto Adapter, class T>
    static TELEMETRY_FORCE_INLINE Scalar invokeContext_(Payload payload) noexcept
    {
        return Adapter(object_<T>(payload));
    }

    static constexpr Function asFunction_(Function function) noexcept { return function; }
    template <class Native>
    static constexpr Native asNative_(Native function) noexcept { return function; }

    Payload payload_{};
    Invoke invoke_ = nullptr;

#undef TELEMETRY_GETTER_NATIVE_TYPES
};

static_assert(std::is_standard_layout_v<Getter> && std::is_trivially_copyable_v<Getter>,
              "Catalog getters must remain trivial non-owning values");
static_assert(sizeof(Getter) == sizeof(void*) * 2,
              "Getter must remain one payload plus one invoker");

} // namespace telemetry

#endif
