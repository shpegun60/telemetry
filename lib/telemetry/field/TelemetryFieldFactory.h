/**
 * @file TelemetryFieldFactory.h
 * @brief Position-identified fields with native compile-time read/write access.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_FIELD_FACTORY_H
#define TELEMETRY_FIELD_FACTORY_H

#include "../detail/TelemetryFieldBinding.h"
#include <tuple>

namespace telemetry {
namespace detail {

// Access is valid only for payloads materialized by the corresponding factory.
// Each union member is read with its original exact type; no pointer punning.
struct FieldTableAccess {
    template <class T> static TELEMETRY_FORCE_INLINE T* object(const Getter& get) noexcept
    { return static_cast<T*>(get.payload_.object); }
    template <class T> static TELEMETRY_FORCE_INLINE T* object(const Setter& set) noexcept
    { return static_cast<T*>(set.payload_.object); }
    template <class T> static TELEMETRY_FORCE_INLINE auto function(const Getter& get) noexcept
    { return Getter::native_(get.payload_, typename Getter::NativeTag<T>{}); }
    template <class T> static TELEMETRY_FORCE_INLINE auto function(const Setter& set) noexcept
    { return Setter::native_(set.payload_, typename Setter::NativeTag<T>{}); }
    template <class T> static TELEMETRY_FORCE_INLINE bool accepts(const FieldType& type, T value) noexcept
    { return type.acceptsNative_(value); }
};

template <auto Read, auto Write, class Owner, class Constraint = NoLimits>
struct StaticFieldAccess {
    using Value = typename FieldBinding<Read, Write, Owner, Constraint>::Value;
    using EnumConstraint = Constraint;
    static constexpr bool writable = !std::is_same_v<decltype(Write), std::nullptr_t>;
    static TELEMETRY_FORCE_INLINE Value read(const Field& entry) noexcept
    {
        if constexpr (std::is_member_function_pointer_v<decltype(Read)>)
            return invokeFactory<Read>(FieldTableAccess::object<Owner>(entry.get));
        else return invokeFactory<Read, NoOwner>(nullptr);
    }
    static TELEMETRY_FORCE_INLINE WriteResult write(const Field& entry, Value value) noexcept
    {
        if constexpr (!writable) return WriteResult::ReadOnly;
        else if constexpr (std::is_member_function_pointer_v<decltype(Write)>)
            return invokeFactory<Write>(FieldTableAccess::object<Owner>(entry.set), value);
        else return invokeFactory<Write, NoOwner>(nullptr, value);
    }
};

template <class Read, class Write = std::nullptr_t>
struct DirectFieldAccess {
    using Value = typename CallableTraits<Read>::Result;
    static constexpr bool writable = !std::is_same_v<Write, std::nullptr_t>;
    static TELEMETRY_FORCE_INLINE Value read(const Field& entry) noexcept
    { return FieldTableAccess::function<Value>(entry.get)(); }
    static TELEMETRY_FORCE_INLINE WriteResult write(const Field& entry, Value value) noexcept
    {
        if constexpr (writable) return FieldTableAccess::function<Value>(entry.set)(value);
        else return WriteResult::ReadOnly;
    }
};

template <class Read, class Write = std::nullptr_t, class Constraint = NoLimits>
struct BorrowedFieldAccess {
    using Value = typename CallableObjectTraits<Read>::Result;
    using EnumConstraint = Constraint;
    static constexpr bool writable = !std::is_same_v<Write, std::nullptr_t>;
    static TELEMETRY_FORCE_INLINE Value read(const Field& entry) noexcept
    { return (*FieldTableAccess::object<Read>(entry.get))(); }
    static TELEMETRY_FORCE_INLINE WriteResult write(const Field& entry, Value value) noexcept
    {
        if constexpr (writable) return (*FieldTableAccess::object<Write>(entry.set))(value);
        else return WriteResult::ReadOnly;
    }
};

struct ManualFieldAccess { using Value = Scalar; };

// Only factories pair an Access type with its matching immutable descriptor.
// That invariant permits reading the original payload directly on typed calls.
// Manual Scalar definitions deliberately retain checked erased dispatch.
template <class Access>
class FieldDefinition {
    const Field entry_;
public:
    using Value = typename Access::Value;
    static constexpr bool hasNativeFastPath = isFactoryValue<Value>;
    constexpr explicit FieldDefinition(Field entry) noexcept : entry_(entry) {}
    constexpr Field materialize() const noexcept { return entry_; }

    template <class T>
    static TELEMETRY_FORCE_INLINE std::optional<T> read(const Field& entry) noexcept
    {
        if constexpr (hasNativeFastPath) {
            // Inferred fields use the getter's exact numeric representation;
            // canonical integer aliases change C++ spelling, not the value.
            using Raw = RawNumberT<Value>;
            using Stored = Scalar::NativeType<Scalar::from(Raw{}).type()>;
            const Stored number = static_cast<Stored>(Access::read(entry));
            return readNumber<T>(number);
        } else return entry.template read<T>();
    }

    static TELEMETRY_FORCE_INLINE auto read(const Field& entry) noexcept
    {
        if constexpr (hasNativeFastPath) {
            using Stored = Scalar::NativeType<Scalar::from(RawNumberT<Value>{}).type()>;
            return read<Stored>(entry);
        } else return entry.read();
    }

    template <class T>
    static TELEMETRY_FORCE_INLINE WriteResult write(const Field& entry, T value) noexcept
    {
        if constexpr (!hasNativeFastPath) return entry.write(value);
        else if constexpr (!Access::writable) return WriteResult::ReadOnly;
        else {
            // Preserve Field::write ordering: availability, numeric conversion,
            // descriptor limits, safe enum cast, then exactly one side effect.
            using Stored = Scalar::NativeType<Scalar::from(RawNumberT<Value>{}).type()>;
            Stored number{};
            if constexpr (std::is_same_v<T, Scalar>) {
                const auto converted = convertScalar<Stored>(value);
                if (!converted) return WriteResult::InvalidValue;
                number = *converted;
            } else if (!convertNumberTo(value, number)) return WriteResult::InvalidValue;
            if (!FieldTableAccess::accepts(entry.declaredType, number)) return WriteResult::InvalidValue;
            // Keep the direct Setter contract for potentially unfixed enums.
            if constexpr (std::is_enum_v<Value> && std::is_convertible_v<Value, int>) {
                if (!acceptsFactoryEnum<Value, typename Access::EnumConstraint>(number))
                    return WriteResult::InvalidValue;
            }
            return Access::write(entry, static_cast<Value>(number));
        }
    }
};

template <class> struct IsFieldDefinition : std::false_type {};
template <class A> struct IsFieldDefinition<FieldDefinition<A>> : std::true_type {};
} // namespace detail

// field retains target types in its result type. FieldTable materializes each
// descriptor once; temporary definition values are not retained.
template <auto Read, auto Write = nullptr, class Owner, class Limits = detail::NoLimits,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !std::is_reference_v<Owner> && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Owner& owner, Limits metadata = {}) noexcept
{
    using Access = detail::StaticFieldAccess<Read, Write, std::remove_reference_t<Owner>, Limits>;
    return detail::FieldDefinition<Access>{detail::materializeField<Read, Write>(name, unit, owner, metadata)};
}
template <auto Read, auto Write = nullptr, class Limits = detail::NoLimits,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write> && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Limits metadata = {}) noexcept
{
    using Access = detail::StaticFieldAccess<Read, Write, detail::NoOwner, Limits>;
    return detail::FieldDefinition<Access>{detail::materializeField<Read, Write>(name, unit, metadata)};
}
template <auto Read, auto Write = nullptr, class Owner,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !std::is_reference_v<Owner>, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type, Owner& owner) noexcept
{
    using Access = detail::StaticFieldAccess<Read, Write, std::remove_reference_t<Owner>>;
    return detail::FieldDefinition<Access>{detail::materializeField<Read, Write>(name, unit, type, owner)};
}
template <auto Read, auto Write = nullptr,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write>, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type) noexcept
{
    using Access = detail::StaticFieldAccess<Read, Write, detail::NoOwner>;
    return detail::FieldDefinition<Access>{detail::materializeField<Read, Write>(name, unit, type)};
}

template <class Read, class Limits = detail::NoLimits,
          class Function = decltype(+std::declval<Read>()),
          std::enable_if_t<detail::HasNativeFunctionPointer<Read>::value
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Read read, Limits metadata = {}) noexcept
{
    // Normalize here before passing by value to the binding layer. Custom
    // conversion objects need not be copied again inside this noexcept body.
    static_assert(noexcept(detail::fieldFunction<Function>(read)),
                  "Factory function-pointer conversion must be noexcept");
    return detail::FieldDefinition<detail::DirectFieldAccess<Function>>{
        detail::materializeField(name, unit, detail::fieldFunction<Function>(read), metadata)};
}
template <class Read, class Write, class Limits = detail::NoLimits,
          class R = decltype(+std::declval<Read>()), class W = decltype(+std::declval<Write>()),
          std::enable_if_t<detail::HasNativeFunctionPointer<Read>::value
              && detail::HasNativeFunctionPointer<Write>::value
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Read read, Write write, Limits metadata = {}) noexcept
{
    static_assert(noexcept(detail::fieldFunction<R>(read)),
                  "Factory getter function-pointer conversion must be noexcept");
    static_assert(noexcept(detail::fieldFunction<W>(write)),
                  "Factory setter function-pointer conversion must be noexcept");
    return detail::FieldDefinition<detail::DirectFieldAccess<R, W>>{
        detail::materializeField(name, unit, detail::fieldFunction<R>(read),
                                 detail::fieldFunction<W>(write), metadata)};
}
template <class Read, class Limits = detail::NoLimits,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Read& read, Limits metadata = {}) noexcept
{
    return detail::FieldDefinition<detail::BorrowedFieldAccess<Read, std::nullptr_t, Limits>>{
        detail::materializeField(name, unit, read, metadata)};
}
template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && std::is_class_v<std::remove_cv_t<Write>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && !detail::HasNativeFunctionPointer<Write>::value
              && !detail::IsLimits<std::remove_cv_t<Write>>::value
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Read& read, Write& write, Limits metadata = {}) noexcept
{
    return detail::FieldDefinition<detail::BorrowedFieldAccess<Read, Write, Limits>>{
        detail::materializeField(name, unit, read, write, metadata)};
}
template <class Read, std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && !detail::HasNativeFunctionPointer<Read>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type, Read& read) noexcept
{
    return detail::FieldDefinition<detail::BorrowedFieldAccess<Read>>{detail::materializeField(name, unit, type, read)};
}
template <class Read, class Write, std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && std::is_class_v<std::remove_cv_t<Write>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && !detail::HasNativeFunctionPointer<Write>::value
              && !detail::IsLimits<std::remove_cv_t<Write>>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type, Read& read, Write& write) noexcept
{
    return detail::FieldDefinition<detail::BorrowedFieldAccess<Read, Write>>{
        detail::materializeField(name, unit, type, read, write)};
}

// A forced template argument such as const Owner must not let a temporary bind
// to the lvalue overload's const reference. Reference template arguments are
// rejected above; these better rvalue matches close the remaining lifetime gap.
template <auto Read, auto Write = nullptr, class Owner, class Limits = detail::NoLimits,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !std::is_reference_v<Owner> && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, Owner&&, Limits = {}) = delete;
template <auto Read, auto Write = nullptr, class Owner,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !std::is_reference_v<Owner>, int> = 0>
auto field(const char*, const char*, FieldType, Owner&&) = delete;

namespace detail {
// Exact class types only: explicitly supplied reference types must not reopen
// the const-reference temporary loophole. Native function-pointer conversions
// keep their by-value overloads, so inline capture-free lambdas still work.
template <class T>
inline constexpr bool isBorrowedFieldCallable = std::is_class_v<std::remove_cv_t<T>>
    && !HasNativeFunctionPointer<T>::value;
template <class Read, class Write, class Limits>
inline constexpr bool isBorrowedFieldPair = isBorrowedFieldCallable<Read>
    && isBorrowedFieldCallable<Write> && !IsLimits<std::remove_cv_t<Write>>::value
    && IsLimits<Limits>::value;
} // namespace detail

template <class Read, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldCallable<Read>
              && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, Read&&, Limits = {}) = delete;
template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, Limits>, int> = 0>
auto field(const char*, const char*, Read&&, Write&, Limits = {}) = delete;
template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, Limits>, int> = 0>
auto field(const char*, const char*, Read&, Write&&, Limits = {}) = delete;
template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, Limits>, int> = 0>
auto field(const char*, const char*, Read&&, Write&&, Limits = {}) = delete;

template <class Read, std::enable_if_t<detail::isBorrowedFieldCallable<Read>, int> = 0>
auto field(const char*, const char*, FieldType, Read&&) = delete;
template <class Read, class Write,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, detail::NoLimits>, int> = 0>
auto field(const char*, const char*, FieldType, Read&&, Write&) = delete;
template <class Read, class Write,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, detail::NoLimits>, int> = 0>
auto field(const char*, const char*, FieldType, Read&, Write&&) = delete;
template <class Read, class Write,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, detail::NoLimits>, int> = 0>
auto field(const char*, const char*, FieldType, Read&&, Write&&) = delete;

constexpr auto field(Field entry) noexcept
{ return detail::FieldDefinition<detail::ManualFieldAccess>{entry}; }
constexpr auto reservedField() noexcept { return field(Field{}); }

} // namespace telemetry
#endif
