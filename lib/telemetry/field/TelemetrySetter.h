/**
 * @file TelemetrySetter.h
 * @brief Compact optional non-owning write callbacks and write results.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_SETTER_H
#define TELEMETRY_SETTER_H

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "../core/TelemetryCompiler.h"
#include "../core/TelemetryScalar.h"

namespace telemetry {

enum class WriteResult : std::uint8_t {
    Applied = 0,
    NotFound,
    ReadOnly,
    InvalidValue,
    Busy,
};

// Field::write normalizes to the declared ScalarType before dispatch. Setter
// then adapts that exact Scalar alternative to the native callback parameter.
// The two-word payload/invoker representation keeps the RW32 write prefix
// unchanged and never calls through a mismatched function-pointer type.
class Setter {
    template <class T> using NativeFunction = WriteResult (*)(T) noexcept;
    template <class T> struct NativeTag {};
    template <class T> struct IsNativeFunction : std::false_type {};
    template <class T>
    struct IsNativeFunction<NativeFunction<T>> : std::bool_constant<detail::isScalarReadType<T>> {};

#define TELEMETRY_SETTER_NATIVE_TYPES(X) \
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
        WriteResult (*scalar)(const Scalar&) noexcept;
#define TELEMETRY_SETTER_MEMBER(Type, Name) NativeFunction<Type> Name;
        TELEMETRY_SETTER_NATIVE_TYPES(TELEMETRY_SETTER_MEMBER)
#undef TELEMETRY_SETTER_MEMBER
#ifdef __cpp_char8_t
        NativeFunction<char8_t> character8;
#endif

        constexpr Payload() noexcept : object(nullptr) {}
        constexpr explicit Payload(void* value) noexcept : object(value) {}
        constexpr explicit Payload(WriteResult (*value)(const Scalar&) noexcept) noexcept
            : scalar(value) {}
#define TELEMETRY_SETTER_CTOR(Type, Name) \
        constexpr Payload(NativeFunction<Type> value, NativeTag<Type>) noexcept : Name(value) {}
        TELEMETRY_SETTER_NATIVE_TYPES(TELEMETRY_SETTER_CTOR)
#undef TELEMETRY_SETTER_CTOR
#ifdef __cpp_char8_t
        constexpr Payload(NativeFunction<char8_t> value, NativeTag<char8_t>) noexcept
            : character8(value) {}
#endif
    };

    using Invoke = WriteResult (*)(Payload, const Scalar&) noexcept;

public:
    using Function = WriteResult (*)(const Scalar&) noexcept;

    constexpr Setter(Function function = nullptr) noexcept
        : payload_(function), invoke_(function != nullptr ? &invokeScalar_ : nullptr) {}

    template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
    constexpr Setter(NativeFunction<T> function) noexcept
        : payload_(function, NativeTag<T>{}),
          invoke_(function != nullptr ? &invokeNative_<T> : nullptr) {}

    template <class F, std::enable_if_t<std::is_class_v<std::decay_t<F>>
              && !std::is_base_of_v<Setter, std::decay_t<F>>
              && std::is_convertible_v<F&&, Function>, int> = 0>
    constexpr Setter(F&& function)
        noexcept(noexcept(asFunction_(std::forward<F>(function))))
        : Setter(asFunction_(std::forward<F>(function))) {}

    template <class F, class Native = decltype(+std::declval<F>()),
              std::enable_if_t<std::is_class_v<std::decay_t<F>>
              && !std::is_base_of_v<Setter, std::decay_t<F>>
              && !std::is_convertible_v<F&&, Function>
              && std::is_pointer_v<Native>
              && std::is_convertible_v<F&&, Native>
              && IsNativeFunction<Native>::value, int> = 0>
    constexpr Setter(F&& function)
        noexcept(noexcept(asNative_<Native>(std::forward<F>(function))))
        : Setter(asNative_<Native>(std::forward<F>(function))) {}

    // At -O2, inlining removes one dispatch boundary. Under GCC/Clang -Os,
    // keeping this two-load adapter out of the caller avoids reserving the
    // invoker register throughout Field::write's conversion/range switch.
    // Both forms use the same two-word representation and observable contract.
    [[nodiscard]]
#if (defined(__GNUC__) || defined(__clang__)) && defined(__OPTIMIZE_SIZE__)
    TELEMETRY_NOINLINE
#else
    TELEMETRY_FORCE_INLINE
#endif
    WriteResult operator()(const Scalar& value) const noexcept
    {
        return invoke_ != nullptr ? invoke_(payload_, value) : WriteResult::ReadOnly;
    }

    constexpr explicit operator bool() const noexcept { return invoke_ != nullptr; }

    // Exact private layout for the link-time ABI signature.
    static constexpr std::size_t abiPayloadOffset() noexcept
    { return offsetof(Setter, payload_); }
    static constexpr std::size_t abiInvokeOffset() noexcept
    { return offsetof(Setter, invoke_); }
    static constexpr std::size_t abiPayloadSize() noexcept { return sizeof(Payload); }
    static constexpr std::size_t abiPayloadAlign() noexcept { return alignof(Payload); }

    template <auto FunctionPointer>
    static constexpr Setter bind() noexcept
    {
        static_assert(std::is_convertible_v<decltype(FunctionPointer), Function>,
                      "The setter must accept const Scalar&, return WriteResult and be noexcept");
        static_assert(FunctionPointer != nullptr, "Setter target cannot be null");
        return Setter(Payload{}, &invokeStatic_<FunctionPointer>);
    }

    template <auto Method, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static constexpr Setter bind(T& object) noexcept
    {
        static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                      "Setter::bind requires a member function");
        static_assert(!std::is_volatile_v<T>, "Setter owners cannot be volatile");
        static_assert(std::is_nothrow_invocable_r_v<WriteResult, decltype(Method), T&, const Scalar&>,
                      "The setter must accept const Scalar&, return WriteResult and be noexcept");
        return Setter(Payload(eraseObject_(std::addressof(object))), &invokeMethod_<Method, T>);
    }

    template <auto Method, class T>
    static Setter bind(T&&) = delete;

    template <auto Adapter, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static constexpr Setter bindContext(T& object) noexcept
    {
        static_assert(!std::is_volatile_v<T>, "Setter contexts cannot be volatile");
        static_assert(std::is_nothrow_invocable_r_v<WriteResult, decltype(Adapter), T&, const Scalar&>,
                      "Setter adapter must accept context and Scalar, return WriteResult and be noexcept");
        return Setter(Payload(eraseObject_(std::addressof(object))), &invokeContext_<Adapter, T>);
    }

    template <auto Adapter, class T>
    static Setter bindContext(T&&) = delete;

private:
    constexpr Setter(Payload payload, Invoke invoke) noexcept
        : payload_(payload), invoke_(invoke) {}

    static TELEMETRY_FORCE_INLINE WriteResult invokeScalar_(Payload payload,
                                                             const Scalar& value) noexcept
    {
        return payload.scalar(value);
    }

#define TELEMETRY_SETTER_ACCESSOR(Type, Name) \
    static constexpr NativeFunction<Type> native_(Payload payload, NativeTag<Type>) noexcept \
    { return payload.Name; }
    TELEMETRY_SETTER_NATIVE_TYPES(TELEMETRY_SETTER_ACCESSOR)
#undef TELEMETRY_SETTER_ACCESSOR
#ifdef __cpp_char8_t
    static constexpr NativeFunction<char8_t> native_(Payload payload, NativeTag<char8_t>) noexcept
    { return payload.character8; }
#endif

    template <class T>
    static TELEMETRY_FORCE_INLINE WriteResult invokeNative_(Payload payload,
                                                             const Scalar& value) noexcept
    {
        constexpr auto type = Scalar::from(T{}).type();
        using Stored = Scalar::NativeType<type>;
        const auto* native = value.template getIf<Stored>();
        return native != nullptr
            ? native_(payload, NativeTag<T>{})(static_cast<T>(*native))
            : WriteResult::InvalidValue;
    }

    template <auto FunctionPointer>
    static TELEMETRY_FORCE_INLINE WriteResult invokeStatic_(Payload,
                                                             const Scalar& value) noexcept
    {
        return FunctionPointer(value);
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
    static TELEMETRY_FORCE_INLINE WriteResult invokeMethod_(Payload payload,
                                                            const Scalar& value) noexcept
    {
        return std::invoke(Method, object_<T>(payload), value);
    }

    template <auto Adapter, class T>
    static TELEMETRY_FORCE_INLINE WriteResult invokeContext_(Payload payload,
                                                             const Scalar& value) noexcept
    {
        return Adapter(object_<T>(payload), value);
    }

    static constexpr Function asFunction_(Function function) noexcept { return function; }
    template <class Native>
    static constexpr Native asNative_(Native function) noexcept { return function; }

    Payload payload_{};
    Invoke invoke_ = nullptr;

#undef TELEMETRY_SETTER_NATIVE_TYPES
};

static_assert(std::is_standard_layout_v<Setter> && std::is_trivially_copyable_v<Setter>,
              "Catalog setters must remain trivial non-owning values");
static_assert(sizeof(Setter) == sizeof(void*) * 2,
              "Setter must remain one payload plus one invoker");

} // namespace telemetry

#endif
