/**
 * @file TelemetryFieldBinding.h
 * @brief Signature-inferred factories producing the concrete RW32 Field.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_BINDING_H
#define TELEMETRY_FIELD_BINDING_H

#include "../field/TelemetryField.h"
#include "../field/TelemetryLimits.h"
#include <memory>

namespace telemetry {
namespace detail {

// Constraint must travel with the generated setter as well as the descriptor.
// It determines safe enum casts even when someone calls Field::set directly.
template <auto Read, auto Write, class Owner, class Constraint = NoLimits>
struct FieldBinding {
    using ReadTraits = CallableTraits<decltype(Read)>;
    using Value = typename ReadTraits::Result;
    using Target = decltype(resolveFactoryOwner(std::declval<Owner*>()));
    static_assert(nonNullTarget<Read>, "Field getter cannot be null");
    static_assert(ReadTraits::arity == 0, "Field getter must have no parameters");
    static_assert(isFactoryValue<Value> || std::is_same_v<Value, Scalar>,
                  "Field getter must return a numeric, enum or Scalar value");

    static TELEMETRY_FORCE_INLINE Scalar nativeRead(Owner& owner) noexcept
    {
        auto* target = resolveFactoryOwner(std::addressof(owner));
        if constexpr (isOwnerSlot<Owner>) {
            if (!target) return Scalar::null();
        }
        return factoryScalar(invokeFactory<Read>(target));
    }
    static RawNumberT<Value> enumReadFree() noexcept
    { return static_cast<RawNumberT<Value>>(invokeFactory<Read, NoOwner>(nullptr)); }

    static TELEMETRY_FORCE_INLINE WriteResult typedWrite(Owner& owner, const Scalar& value) noexcept
    {
        auto* target = resolveFactoryOwner(std::addressof(owner));
        if constexpr (isOwnerSlot<Owner>) {
            if (!target) return WriteResult::Unavailable;
        }
        if constexpr (std::is_same_v<Value, Scalar>) return invokeFactory<Write>(target, value);
        else {
            return invokeFactoryValue<Value, Constraint>(value,
                [](Value native, Target resolved) noexcept { return invokeFactory<Write>(resolved, native); },
                target);
        }
    }
    static TELEMETRY_FORCE_INLINE WriteResult typedWriteFree(const Scalar& value) noexcept
    {
        if constexpr (std::is_same_v<Value, Scalar>) return invokeFactory<Write, NoOwner>(nullptr, value);
        else {
            return invokeFactoryValue<Value, Constraint>(value,
                [](Value native) noexcept { return invokeFactory<Write, NoOwner>(nullptr, native); });
        }
    }

    static constexpr Getter getter(Owner* owner) noexcept
    {
        if constexpr (ReadTraits::member) {
            if constexpr (std::is_enum_v<Value> || isOwnerSlot<Owner>) return Getter::bindKnownContext_<&nativeRead>(*owner);
            else return Getter::bind<Read>(*owner);
        } else {
            if constexpr (std::is_enum_v<Value>) return Getter::bindKnown_<&enumReadFree>();
            else return Getter::bind<Read>();
        }
    }

    static constexpr Setter setter(Owner* owner) noexcept
    {
        if constexpr (std::is_same_v<decltype(Write), std::nullptr_t>) return nullptr;
        else {
            using Traits = CallableTraits<decltype(Write)>;
            static_assert(nonNullTarget<Write>, "Field setter cannot be a null function pointer");
            static_assert(std::is_same_v<typename Traits::Result, WriteResult>, "Field setter must return WriteResult");
            static_assert(Traits::arity == 1, "Field setter must have exactly one parameter");
            using Arg = std::tuple_element_t<0, typename Traits::Arguments>;
            static_assert(std::is_same_v<Arg, Value>
                          || (std::is_same_v<Value, Scalar> && std::is_same_v<Arg, const Scalar&>),
                          "Field getter and setter must use the exact same C++ type");
            if constexpr (Traits::member) return Setter::bindKnownContext_<&typedWrite>(*owner);
            else return Setter::bindKnown_<&typedWriteFree>();
        }
    }

    static constexpr Field make(const char* name, const char* unit,
                                Owner* owner, FieldType type) noexcept
    {
        return Field{name, unit, type, getter(owner), setter(owner)};
    }
};

template <auto Read, auto Write>
inline constexpr bool fieldNeedsOwner = std::is_member_function_pointer_v<decltype(Read)>
    || std::is_member_function_pointer_v<decltype(Write)>;

template <class Function> constexpr Function fieldFunction(Function value) noexcept { return value; }

// Parameter-form callbacks retain their exact native function-pointer types in
// Getter/Setter. The finite numeric/bool type set makes this constexpr in C++17
// without erasing or casting a function pointer.
template <class ReadFunction>
struct DirectFieldReadBinding {
    using Traits = CallableTraits<ReadFunction>;
    using Value = typename Traits::Result;
    static_assert(Traits::arity == 0, "Field getter must have no parameters");
    static_assert(isScalarReadType<Value>,
                  "Direct Field callbacks must use a native numeric or bool type; use NTTP callbacks for enums");

    static constexpr Getter getter(ReadFunction function) noexcept
    {
        return Getter(function);
    }
};

template <class ReadFunction, class WriteFunction>
struct DirectFieldPairBinding : DirectFieldReadBinding<ReadFunction> {
    using Base = DirectFieldReadBinding<ReadFunction>;
    using Value = typename Base::Value;
    using WriteTraits = CallableTraits<WriteFunction>;
    static_assert(std::is_same_v<typename WriteTraits::Result, WriteResult>,
                  "Field setter must return WriteResult");
    static_assert(WriteTraits::arity == 1,
                  "Field setter must have exactly one parameter");
    using Argument = std::tuple_element_t<0, typename WriteTraits::Arguments>;
    static_assert(std::is_same_v<Argument, Value>,
                  "Field getter and setter must use the exact same C++ type");

    static constexpr Setter setter(WriteFunction function) noexcept
    {
        return Setter(function);
    }
};

// Stateful functors and capturing lambdas are kept outside Field and borrowed
// by address. Getter/Setter still contain exactly one payload and one invoker;
// these adapters add neither ownership nor storage to the descriptor.
template <class ReadCallable>
struct BorrowedFieldReadBinding {
    static_assert(hasFactoryCallSignature<ReadCallable>,
                  "Borrowed field getter must have one concrete operator(); generic and overloaded callables are unsupported");
    static_assert(!std::is_volatile_v<ReadCallable>,
                  "Borrowed field getter cannot be volatile");
    using Traits = CallableObjectTraits<ReadCallable>;
    using Value = typename Traits::Result;
    static_assert(Traits::arity == 0, "Field getter must have no parameters");
    static_assert(isFactoryValue<Value> || std::is_same_v<Value, Scalar>,
                  "Field getter must return a numeric, enum or Scalar value");

    static TELEMETRY_FORCE_INLINE Scalar read(ReadCallable& callable) noexcept
    {
        auto target = resolveFactoryCallable(std::addressof(callable));
        if constexpr (isCallableSlot<ReadCallable>) {
            if (!target) return Scalar::null();
        }
        return factoryScalar(invokeResolvedCallable(target));
    }

    static constexpr Getter getter(ReadCallable& callable) noexcept
    {
        return Getter::bindKnownContext_<&read>(callable);
    }
};

template <class ReadCallable, class WriteCallable, class Constraint = NoLimits>
struct BorrowedFieldPairBinding : BorrowedFieldReadBinding<ReadCallable> {
    using Base = BorrowedFieldReadBinding<ReadCallable>;
    using Value = typename Base::Value;
    using Target = decltype(resolveFactoryCallable(std::declval<WriteCallable*>()));
    static_assert(hasFactoryCallSignature<WriteCallable>,
                  "Borrowed field setter must have one concrete operator(); generic and overloaded callables are unsupported");
    static_assert(!std::is_volatile_v<WriteCallable>,
                  "Borrowed field setter cannot be volatile");
    using WriteTraits = CallableObjectTraits<WriteCallable>;
    static_assert(std::is_same_v<typename WriteTraits::Result, WriteResult>,
                  "Field setter must return WriteResult");
    static_assert(WriteTraits::arity == 1,
                  "Field setter must have exactly one parameter");
    using Argument = std::tuple_element_t<0, typename WriteTraits::Arguments>;
    static_assert(std::is_same_v<Argument, Value>
                  || (std::is_same_v<Value, Scalar> && std::is_same_v<Argument, const Scalar&>),
                  "Field getter and setter must use the exact same C++ type");

    static TELEMETRY_FORCE_INLINE WriteResult write(WriteCallable& callable,
                                                     const Scalar& value) noexcept
    {
        auto target = resolveFactoryCallable(std::addressof(callable));
        if constexpr (isCallableSlot<WriteCallable>) {
            if (!target) return WriteResult::Unavailable;
        }
        if constexpr (std::is_same_v<Value, Scalar>) return invokeResolvedCallable(target, value);
        else if constexpr (IsContextCallableSlot<std::remove_cv_t<WriteCallable>>::value) {
            // Pass the snapshot's two words separately so the fallback does not
            // require a temporary aggregate in the matching-tag path.
            return invokeFactoryValue<Value, Constraint>(value,
                [](Value native, void* context,
                   typename std::remove_cv_t<WriteCallable>::Function function) noexcept { return function(context, native); },
                target.context, target.function);
        } else {
            return invokeFactoryValue<Value, Constraint>(value,
                [](Value native, Target resolved) noexcept { return invokeResolvedCallable(resolved, native); },
                target);
        }
    }

    static constexpr Setter setter(WriteCallable& callable) noexcept
    {
        return Setter::bindKnownContext_<&write>(callable);
    }
};

// An inline capture-free lambda or ordinary function pointer is stored as its
// native function pointer. No closure object survives this factory call.
template <class F, class Limits = detail::NoLimits,
          class Function = decltype(+std::declval<F>()),
          std::enable_if_t<std::is_pointer_v<Function>
              && std::is_function_v<std::remove_pointer_t<Function>>
              && std::is_convertible_v<F, Function> && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          F function, Limits metadata = {}) noexcept
{
    using Traits = detail::CallableTraits<Function>;
    using Value = typename Traits::Result;
    static_assert(Traits::arity == 0 && detail::isScalarReadType<Value>,
                  "Inline factory getter must return a numeric value; use a named template target for enums");
    static_assert(noexcept(detail::fieldFunction<Function>(function)),
                  "Factory function-pointer conversion must be noexcept");
    const Function target = function;
    if (!target) detail::invalidFieldLimits();
    return Field{name, unit, detail::refineType<Value>(metadata),
                 detail::DirectFieldReadBinding<Function>::getter(target)};
}

// The parameter form keeps both callbacks as parameters: no callback is
// silently promoted to an NTTP. Both exact pointers remain constexpr-capable.
template <class Read, class Write, class Limits = detail::NoLimits,
          class ReadFunction = decltype(+std::declval<Read>()),
          class WriteFunction = decltype(+std::declval<Write>()),
          std::enable_if_t<std::is_pointer_v<ReadFunction>
              && std::is_function_v<std::remove_pointer_t<ReadFunction>>
              && std::is_convertible_v<Read, ReadFunction>
              && std::is_pointer_v<WriteFunction>
              && std::is_function_v<std::remove_pointer_t<WriteFunction>>
              && std::is_convertible_v<Write, WriteFunction>
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          Read read, Write write, Limits metadata = {}) noexcept
{
    using Binding = detail::DirectFieldPairBinding<ReadFunction, WriteFunction>;
    static_assert(noexcept(detail::fieldFunction<ReadFunction>(read)),
                  "Factory getter function-pointer conversion must be noexcept");
    static_assert(noexcept(detail::fieldFunction<WriteFunction>(write)),
                  "Factory setter function-pointer conversion must be noexcept");
    const ReadFunction getter = read;
    const WriteFunction setter = write;
    if (getter == nullptr || setter == nullptr) detail::invalidFieldLimits();
    return Field{name, unit, detail::refineType<typename Binding::Value>(metadata),
                 Binding::getter(getter), Binding::setter(setter)};
}

// Capturing lambdas and stateful functors are non-owning bindings. Both the
// callable object and every object it captures must outlive all Field copies.
// Capture-free lambdas remain on the direct function-pointer overloads above.
template <class Read, class Limits = detail::NoLimits,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          Read& read, Limits metadata = {}) noexcept
{
    using Binding = detail::BorrowedFieldReadBinding<Read>;
    return Field{name, unit, detail::refineType<typename Binding::Value>(metadata),
                 Binding::getter(read)};
}

template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && std::is_class_v<std::remove_cv_t<Write>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && !detail::HasNativeFunctionPointer<Write>::value
              && !detail::IsLimits<std::remove_cv_t<Write>>::value
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          Read& read, Write& write, Limits metadata = {}) noexcept
{
    using Binding = detail::BorrowedFieldPairBinding<Read, Write, Limits>;
    return Field{name, unit, detail::refineType<typename Binding::Value>(metadata),
                 Binding::getter(read), Binding::setter(write)};
}

// A Scalar-returning borrowed callback cannot imply its runtime alternative.
// Keep the same explicit-type escape hatch as the NTTP factory forms.
template <class Read,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && !detail::HasNativeFunctionPointer<Read>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          FieldType type, Read& read) noexcept
{
    using Binding = detail::BorrowedFieldReadBinding<Read>;
    static_assert(std::is_same_v<typename Binding::Value, Scalar>,
                  "Explicit FieldType is reserved for Scalar-returning getters");
    return Field{name, unit, type, Binding::getter(read)};
}

template <class Read, class Write,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && std::is_class_v<std::remove_cv_t<Write>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && !detail::HasNativeFunctionPointer<Write>::value
              && !detail::IsLimits<std::remove_cv_t<Write>>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          FieldType type, Read& read, Write& write) noexcept
{
    using Binding = detail::BorrowedFieldPairBinding<Read, Write>;
    static_assert(std::is_same_v<typename Binding::Value, Scalar>,
                  "Explicit FieldType is reserved for Scalar-returning getters");
    return Field{name, unit, type, Binding::getter(read), Binding::setter(write)};
}

// Owners are borrowed lvalues. Metadata is consumed by value, so inline
// limits(...) is safe. The result is the original immutable Field type.
template <auto Read, auto Write = nullptr, class Owner, class Limits = detail::NoLimits,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
                           && std::is_lvalue_reference_v<Owner&&>
                           && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          Owner&& owner, Limits metadata = {}) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, std::remove_reference_t<Owner>, Limits>;
    return Binding::make(name, unit, std::addressof(owner),
                         detail::refineType<typename Binding::Value>(metadata));
}

template <auto Read, auto Write = nullptr, class Limits = detail::NoLimits,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write>
                           && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field materializeField(const char* name, const char* unit, Limits metadata = {}) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, detail::NoOwner, Limits>;
    return Binding::make(name, unit, nullptr, detail::refineType<typename Binding::Value>(metadata));
}

// Scalar-returning escape hatch: runtime alternatives cannot imply a type.
template <auto Read, auto Write = nullptr, class Owner,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
                           && std::is_lvalue_reference_v<Owner&&>, int> = 0>
constexpr Field materializeField(const char* name, const char* unit,
                          FieldType type, Owner&& owner) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, std::remove_reference_t<Owner>>;
    static_assert(std::is_same_v<typename Binding::Value, Scalar>,
                  "Explicit FieldType is reserved for Scalar-returning getters");
    return Binding::make(name, unit, std::addressof(owner), type);
}

template <auto Read, auto Write = nullptr,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write>, int> = 0>
constexpr Field materializeField(const char* name, const char* unit, FieldType type) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, detail::NoOwner>;
    static_assert(std::is_same_v<typename Binding::Value, Scalar>,
                  "Explicit FieldType is reserved for Scalar-returning getters");
    return Binding::make(name, unit, nullptr, type);
}
} // namespace detail
} // namespace telemetry
#endif
