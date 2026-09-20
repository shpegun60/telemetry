/**
 * @file TelemetryFieldFactory.h
 * @brief Signature-inferred factories producing the concrete RW32 Field.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_FACTORY_H
#define TELEMETRY_FIELD_FACTORY_H

#include "TelemetryField.h"
#include "TelemetryLimits.h"
#include <memory>

namespace telemetry {
namespace detail {

template <auto Read, auto Write, class Owner>
struct FieldBinding {
    using ReadTraits = CallableTraits<decltype(Read)>;
    using Value = typename ReadTraits::Result;
    static_assert(Read != nullptr, "Field getter cannot be null");
    static_assert(ReadTraits::arity == 0, "Field getter must have no parameters");
    static_assert(isFactoryValue<Value> || std::is_same_v<Value, Scalar>,
                  "Field getter must return a numeric, enum or Scalar value");

    static TELEMETRY_FORCE_INLINE Scalar nativeRead(Owner& owner) noexcept
    {
        return factoryScalar(invokeFactory<Read>(std::addressof(owner)));
    }
    static RawNumberT<Value> enumReadFree() noexcept
    { return static_cast<RawNumberT<Value>>(invokeFactory<Read, NoOwner>(nullptr)); }

    static TELEMETRY_FORCE_INLINE WriteResult typedWrite(Owner& owner, const Scalar& value) noexcept
    {
        if constexpr (std::is_same_v<Value, Scalar>) return invokeFactory<Write>(std::addressof(owner), value);
        else {
            Value native{};
            if (!extractFactoryValue(value, native)) return WriteResult::InvalidValue;
            return invokeFactory<Write>(std::addressof(owner), native);
        }
    }
    static TELEMETRY_FORCE_INLINE WriteResult typedWriteFree(const Scalar& value) noexcept
    {
        if constexpr (std::is_same_v<Value, Scalar>) return invokeFactory<Write, NoOwner>(nullptr, value);
        else {
            Value native{};
            if (!extractFactoryValue(value, native)) return WriteResult::InvalidValue;
            return invokeFactory<Write, NoOwner>(nullptr, native);
        }
    }

    static constexpr Getter getter(Owner* owner) noexcept
    {
        if constexpr (ReadTraits::member) {
            if constexpr (std::is_enum_v<Value>) return Getter::bindContext<&nativeRead>(*owner);
            else return Getter::bind<Read>(*owner);
        } else {
            if constexpr (std::is_enum_v<Value>) return Getter(&enumReadFree);
            else return Getter(Read);
        }
    }

    static constexpr Setter setter(Owner* owner) noexcept
    {
        if constexpr (std::is_same_v<decltype(Write), std::nullptr_t>) return nullptr;
        else {
            using Traits = CallableTraits<decltype(Write)>;
            static_assert(Write != nullptr, "Field setter cannot be a null function pointer");
            static_assert(std::is_same_v<typename Traits::Result, WriteResult>, "Field setter must return WriteResult");
            static_assert(Traits::arity == 1, "Field setter must have exactly one parameter");
            using Arg = std::tuple_element_t<0, typename Traits::Arguments>;
            static_assert(std::is_same_v<Arg, Value>
                          || (std::is_same_v<Value, Scalar> && std::is_same_v<Arg, const Scalar&>),
                          "Field getter and setter must use the exact same C++ type");
            if constexpr (Traits::member) return Setter::bindContext<&typedWrite>(*owner);
            else return Setter::bind<&typedWriteFree>();
        }
    }

    static constexpr Field make(FieldId id, const char* name, const char* unit,
                                Owner* owner, FieldType type) noexcept
    {
        return Field{id, name, unit, type, getter(owner), setter(owner)};
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
} // namespace detail

// An inline capture-free lambda or ordinary function pointer is stored as its
// native function pointer. No closure object survives this factory call.
template <class F, class Limits = detail::NoLimits,
          class Function = decltype(+std::declval<F>()),
          std::enable_if_t<std::is_pointer_v<Function>
              && std::is_function_v<std::remove_pointer_t<Function>>
              && std::is_convertible_v<F, Function> && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field makeField(FieldId id, const char* name, const char* unit,
                          F function, Limits metadata = {}) noexcept
{
    using Traits = detail::CallableTraits<Function>;
    using Value = typename Traits::Result;
    static_assert(Traits::arity == 0 && detail::isScalarReadType<Value>,
                  "Inline factory getter must return a numeric value; use a named template target for enums");
    static_assert(noexcept(detail::fieldFunction<Function>(function)),
                  "Factory function-pointer conversion must be noexcept");
    const Function target = function;
    if (target == nullptr) detail::invalidFieldLimits();
    return Field{id, name, unit, detail::refineType<Value>(metadata),
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
constexpr Field makeField(FieldId id, const char* name, const char* unit,
                          Read read, Write write, Limits metadata = {}) noexcept
{
    using Binding = detail::DirectFieldPairBinding<ReadFunction, WriteFunction>;
    const ReadFunction getter = read;
    const WriteFunction setter = write;
    if (getter == nullptr || setter == nullptr) detail::invalidFieldLimits();
    return Field{id, name, unit, detail::refineType<typename Binding::Value>(metadata),
                 Binding::getter(getter), Binding::setter(setter)};
}

// Owners are borrowed lvalues. Metadata is consumed by value, so inline
// limits(...) is safe. The result is the original immutable Field type.
template <auto Read, auto Write = nullptr, class Owner, class Limits = detail::NoLimits,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
                           && std::is_lvalue_reference_v<Owner&&>
                           && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field makeField(FieldId id, const char* name, const char* unit,
                          Owner&& owner, Limits metadata = {}) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, std::remove_reference_t<Owner>>;
    return Binding::make(id, name, unit, std::addressof(owner),
                         detail::refineType<typename Binding::Value>(metadata));
}

template <auto Read, auto Write = nullptr, class Limits = detail::NoLimits,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write>
                           && detail::IsLimits<Limits>::value, int> = 0>
constexpr Field makeField(FieldId id, const char* name, const char* unit, Limits metadata = {}) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, detail::NoOwner>;
    return Binding::make(id, name, unit, nullptr, detail::refineType<typename Binding::Value>(metadata));
}

// Scalar-returning escape hatch: runtime alternatives cannot imply a type.
template <auto Read, auto Write = nullptr, class Owner,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
                           && std::is_lvalue_reference_v<Owner&&>, int> = 0>
constexpr Field makeField(FieldId id, const char* name, const char* unit,
                          FieldType type, Owner&& owner) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, std::remove_reference_t<Owner>>;
    static_assert(std::is_same_v<typename Binding::Value, Scalar>,
                  "Explicit FieldType is reserved for Scalar-returning getters");
    return Binding::make(id, name, unit, std::addressof(owner), type);
}

template <auto Read, auto Write = nullptr,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write>, int> = 0>
constexpr Field makeField(FieldId id, const char* name, const char* unit, FieldType type) noexcept
{
    using Binding = detail::FieldBinding<Read, Write, detail::NoOwner>;
    static_assert(std::is_same_v<typename Binding::Value, Scalar>,
                  "Explicit FieldType is reserved for Scalar-returning getters");
    return Binding::make(id, name, unit, nullptr, type);
}
} // namespace telemetry
#endif
