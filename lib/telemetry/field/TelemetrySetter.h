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
#include "../core/TelemetryConversion.h"
#include "../detail/TelemetryOwner.h"
#include "../detail/TelemetryTarget.h"

namespace telemetry {
namespace detail {
struct FieldTableAccess;
template <auto, auto, class, class> struct FieldBinding;
template <class, class, class> struct BorrowedFieldPairBinding;
}

enum class WriteResult : std::uint8_t {
    Applied = 0,
    NotFound,
    ReadOnly,
    InvalidValue,
    Busy,
    Unavailable, // A late-bound owner slot is empty; existing result codes stay unchanged.
};

// Field::write normalizes to the declared ScalarType before dispatch. Setter
// then converts to the native callback parameter if its representation differs.
// The two-word payload/invoker representation keeps the RW32 write prefix
// unchanged and never calls through a mismatched function-pointer type.
class Setter {
    friend struct detail::FieldTableAccess;
    template <auto, auto, class, class> friend struct detail::FieldBinding;
    template <class, class, class> friend struct detail::BorrowedFieldPairBinding;
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

    // Native callbacks keep their exact C++ type, including distinct integer
    // aliases of the same width. The paired invoker selects the active member.
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
        : payload_(function),
          invoke_(detail::pointerPresent(function)
              ? (detail::pointerPresenceUncertain(function) ? &invokeScalarChecked_ : &invokeScalar_)
              : nullptr) {}

    template <class T, std::enable_if_t<detail::isScalarReadType<T>, int> = 0>
    constexpr Setter(NativeFunction<T> function) noexcept
        : payload_(function, NativeTag<T>{}),
          invoke_(detail::pointerPresent(function)
              ? (detail::pointerPresenceUncertain(function) ? &invokeNativeChecked_<T> : &invokeNative_<T>)
              : nullptr) {}

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

    // Keep one inline definition across translation units compiled at different
    // optimization levels. The compiler chooses the size/speed trade-off.
    [[nodiscard]]
    WriteResult operator()(const Scalar& value) const noexcept
    {
        return invoke_ != nullptr ? invoke_(payload_, value) : WriteResult::ReadOnly;
    }

    constexpr explicit operator bool() const noexcept { return detail::pointerPresent(invoke_); }

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
        static_assert(detail::nonNullTarget<static_cast<Function>(FunctionPointer)>,
                      "Setter target cannot be null");
        // C++20 structural objects may convert to a noexcept pointer yet have
        // a throwing call operator. invokeStatic_ calls the object itself.
        static_assert(std::is_nothrow_invocable_r_v<WriteResult, decltype((FunctionPointer)), const Scalar&>,
                      "The actual setter invocation must be noexcept");
        return bindKnown_<FunctionPointer>();
    }

    template <auto Method, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static constexpr Setter bind(T& object) noexcept
    {
        static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                      "Setter::bind requires a member function");
        static_assert(detail::nonNullTarget<Method>, "Setter target cannot be null");
        static_assert(detail::isDirectMemberOwner<decltype(Method), T>,
                      "Setter owner must be the actual object or a derived object; dereference pointers explicitly or use OwnerSlot");
        static_assert(!std::is_volatile_v<T>, "Setter owners cannot be volatile");
        static_assert(std::is_nothrow_invocable_r_v<WriteResult, decltype(Method), T&, const Scalar&>,
                      "The setter must accept const Scalar&, return WriteResult and be noexcept");
        return Setter(Payload(eraseObject_(std::addressof(object))), &invokeMethod_<Method, T>);
    }

    template <auto Method, class T = void, class Argument,
              std::enable_if_t<!detail::isBorrowedObjectArgument<T, Argument>, int> = 0>
    static Setter bind(Argument&&) = delete;

    // Braces cannot deduce Argument. Keep an explicitly typed rvalue guard,
    // and reject braced conversion proxies without rejecting {stableObject}.
    // A named initializer_list lvalue may be borrowed if kept alive by caller.
    template <auto Method, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static Setter bind(std::remove_reference_t<T>&&) = delete;
    template <auto Method, class T, class Argument,
              std::enable_if_t<!detail::isBorrowedObjectArgument<T, Argument&>, int> = 0>
    static Setter bind(std::initializer_list<Argument>&&) = delete;

    template <auto Adapter, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static constexpr Setter bindContext(T& object) noexcept
    {
        // A null function pointer is type-invocable but cannot be called.
        if constexpr (std::is_pointer_v<decltype(Adapter)>)
            static_assert(detail::nonNullTarget<Adapter>, "Setter adapter cannot be null");
        static_assert(!std::is_volatile_v<T>, "Setter contexts cannot be volatile");
        // Match the const-lvalue expression used for a structural NTTP adapter.
        static_assert(std::is_nothrow_invocable_r_v<WriteResult, decltype((Adapter)), T&, const Scalar&>,
                      "Setter adapter must accept context and Scalar, return WriteResult and be noexcept");
        return bindKnownContext_<Adapter>(object);
    }

    template <auto Adapter, class T = void, class Argument,
              std::enable_if_t<!detail::isBorrowedObjectArgument<T, Argument>, int> = 0>
    static Setter bindContext(Argument&&) = delete;

    // Braces cannot deduce Argument. Keep an explicitly typed rvalue guard,
    // and reject braced conversion proxies without rejecting {stableObject}.
    // A named initializer_list lvalue may be borrowed if kept alive by caller.
    template <auto Adapter, class T, std::enable_if_t<!std::is_reference_v<T>, int> = 0>
    static Setter bindContext(std::remove_reference_t<T>&&) = delete;
    template <auto Adapter, class T, class Argument,
              std::enable_if_t<!detail::isBorrowedContextListElement<T, Argument>, int> = 0>
    static Setter bindContext(std::initializer_list<Argument>&&) = delete;

private:
    constexpr Setter(Payload payload, Invoke invoke) noexcept
        : payload_(payload), invoke_(invoke) {}

    // These private paths accept only adapters defined by the field binding
    // layer. Public function and context targets still validate their address.
    template <auto FunctionPointer>
    static constexpr Setter bindKnown_() noexcept
    { return Setter(Payload{}, &invokeStatic_<FunctionPointer>); }
    template <auto Adapter, class T>
    static constexpr Setter bindKnownContext_(T& object) noexcept
    {
        return Setter(Payload(eraseObject_(std::addressof(object))), &invokeContext_<Adapter, T>);
    }

    static inline WriteResult invokeScalar_(Payload payload,
                                                             const Scalar& value) noexcept
    {
        return payload.scalar(value);
    }

    static inline WriteResult invokeScalarChecked_(Payload payload,
                                                                    const Scalar& value) noexcept
    {
        const auto function = payload.scalar;
        return function != nullptr ? function(value) : WriteResult::Unavailable;
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
    static TELEMETRY_OPTIMIZE_SPEED inline WriteResult invokeNative_(Payload payload,
                                                             const Scalar& value) noexcept
    {
        constexpr auto type = Scalar::from(T{}).type();
        using Stored = Scalar::NativeType<type>;
        // Inferred descriptors reach this exact-type branch. Manual Fields
        // may declare another wire type; preserve its normalization, then
        // perform a checked conversion to the callback's actual C++ type.
        const auto* native = value.template getIf<Stored>();
        if (native != nullptr)
            return native_(payload, NativeTag<T>{})(static_cast<T>(*native));
        return invokeConverted_<T>(native_(payload, NativeTag<T>{}), value);
    }

    template <class T>
    static inline WriteResult invokeNativeChecked_(Payload payload,
                                                                    const Scalar& value) noexcept
    {
        return native_(payload, NativeTag<T>{}) != nullptr
            ? invokeNative_<T>(payload, value) : WriteResult::Unavailable;
    }

    // A manual row can use a different declared type. Keep its uncommon
    // conversion out of the exact-tag thunk, so ordinary erased writes do not
    // reserve a conversion frame or save registers for this fallback at -Os.
    template <class T>
    static TELEMETRY_NOINLINE WriteResult invokeConverted_(NativeFunction<T> function,
                                                          const Scalar& value) noexcept
    {
        const auto converted = convertScalar<T>(value);
        return converted
            ? function(*converted)
            : WriteResult::InvalidValue;
    }

    // Generated native adapters also carry a checked-conversion fallback.
    // Optimize their emitted thunks like invokeNative_: on ARM GCC 14 this
    // keeps the matching-tag path stackless even in a size-optimized build.
    template <auto FunctionPointer>
    static TELEMETRY_OPTIMIZE_SPEED inline WriteResult invokeStatic_(Payload,
                                                             const Scalar& value) noexcept
    {
        if (!detail::targetAvailable<FunctionPointer>()) return WriteResult::Unavailable;
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
        // The invoker restores the original T, including const qualification.
        return const_cast<void*>(static_cast<const volatile void*>(pointer));
    }

    template <auto Method, class T>
    static inline WriteResult invokeMethod_(Payload payload,
                                                            const Scalar& value) noexcept
    {
        if (!detail::targetAvailable<Method>()) return WriteResult::Unavailable;
        return std::invoke(Method, object_<T>(payload), value);
    }

    template <auto Adapter, class T>
    static TELEMETRY_OPTIMIZE_SPEED inline WriteResult invokeContext_(Payload payload,
                                                             const Scalar& value) noexcept
    {
        if (!detail::targetAvailable<Adapter>()) return WriteResult::Unavailable;
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
