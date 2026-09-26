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

template <class Access> class FieldDefinition;

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
    static constexpr bool checkReadTarget = true;
    static constexpr bool nullableRead = isOwnerSlot<Owner>
        && std::is_member_function_pointer_v<decltype(Read)>;

    static TELEMETRY_FORCE_INLINE bool readTargetAvailable(const Field&) noexcept
    { return targetAvailable<Read>(); }

    // The access type creates its own descriptor. Keeping these inputs typed
    // prevents an unrelated Field payload from being paired with this accessor.
    static constexpr FieldDefinition<StaticFieldAccess> make(
        const char* name, const char* unit, Owner* owner, Constraint metadata = {}) noexcept
    {
        return FieldDefinition<StaticFieldAccess>{
            FieldBinding<Read, Write, Owner, Constraint>::make(
                name, unit, owner, refineType<Value>(metadata))};
    }
    static constexpr FieldDefinition<StaticFieldAccess> makeScalar(
        const char* name, const char* unit, FieldType type, Owner* owner) noexcept
    {
        static_assert(std::is_same_v<Value, Scalar>,
                      "Explicit FieldType is reserved for Scalar-returning getters");
        return FieldDefinition<StaticFieldAccess>{
            FieldBinding<Read, Write, Owner, Constraint>::make(name, unit, owner, type)};
    }
    static TELEMETRY_FORCE_INLINE Value read(const Field& entry) noexcept
    {
        static_assert(!nullableRead, "Slot reads must use the optional readSlot adapter");
        if constexpr (std::is_member_function_pointer_v<decltype(Read)>)
            return invokeFactory<Read>(FieldTableAccess::object<Owner>(entry.get));
        else return invokeFactory<Read, NoOwner>(nullptr);
    }
    template <class T>
    static TELEMETRY_FORCE_INLINE std::optional<T> readSlot(const Field& entry) noexcept
    {
        // Normalize directly into the caller's optional, avoiding an intermediate
        // optional<Value> that prevents a tail call on GCC 13 at -Os.
        auto* target = resolveFactoryOwner(FieldTableAccess::object<Owner>(entry.get));
        if (!target) return std::nullopt;
        if (!readTargetAvailable(entry)) return std::nullopt;
        using Stored = Scalar::NativeType<Scalar::from(RawNumberT<Value>{}).type()>;
        return readNumber<T>(static_cast<Stored>(invokeFactory<Read>(target)));
    }
    static TELEMETRY_FORCE_INLINE WriteResult write(const Field& entry, Value value) noexcept
    {
        if constexpr (!writable) return WriteResult::ReadOnly;
        else if constexpr (std::is_member_function_pointer_v<decltype(Write)>) {
            auto* target = resolveFactoryOwner(FieldTableAccess::object<Owner>(entry.set));
            if constexpr (isOwnerSlot<Owner>) {
                if (!target) return WriteResult::Unavailable;
            }
            if (!targetAvailable<Write>()) return WriteResult::Unavailable;
            return invokeFactory<Write>(target, value);
        }
        else return targetAvailable<Write>()
            ? invokeFactory<Write, NoOwner>(nullptr, value) : WriteResult::Unavailable;
    }
};

template <class Read, class Write = std::nullptr_t>
struct DirectFieldAccess {
    using Value = typename CallableTraits<Read>::Result;
    static constexpr bool nullableRead = false;
    static constexpr bool checkReadTarget = true;
    static constexpr bool writable = !std::is_same_v<Write, std::nullptr_t>;

    static TELEMETRY_FORCE_INLINE bool readTargetAvailable(const Field& entry) noexcept
    { return FieldTableAccess::function<Value>(entry.get) != nullptr; }

    template <class Limits>
    static constexpr FieldDefinition<DirectFieldAccess> make(
        const char* name, const char* unit, Read read, Write write, Limits metadata) noexcept
    {
        if constexpr (writable) {
            return FieldDefinition<DirectFieldAccess>{
                materializeField(name, unit, read, write, metadata)};
        } else {
            return FieldDefinition<DirectFieldAccess>{
                materializeField(name, unit, read, metadata)};
        }
    }
    static TELEMETRY_FORCE_INLINE Value read(const Field& entry) noexcept
    { return FieldTableAccess::function<Value>(entry.get)(); }
    static TELEMETRY_FORCE_INLINE WriteResult write(const Field& entry, Value value) noexcept
    {
        if constexpr (writable) {
            const auto function = FieldTableAccess::function<Value>(entry.set);
            return function != nullptr ? function(value) : WriteResult::Unavailable;
        }
        else return WriteResult::ReadOnly;
    }
};

template <class Read, class Write = std::nullptr_t, class Constraint = NoLimits>
struct BorrowedFieldAccess {
    using Value = typename CallableObjectTraits<Read>::Result;
    using EnumConstraint = Constraint;
    static constexpr bool nullableRead = isCallableSlot<Read>;
    static constexpr bool checkReadTarget = false;
    static constexpr bool writable = !std::is_same_v<Write, std::nullptr_t>;

    static constexpr FieldDefinition<BorrowedFieldAccess> make(
        const char* name, const char* unit, Read* read, Write* write, Constraint metadata = {}) noexcept
    {
        if constexpr (writable) {
            return FieldDefinition<BorrowedFieldAccess>{
                materializeField(name, unit, *read, *write, metadata)};
        } else {
            return FieldDefinition<BorrowedFieldAccess>{
                materializeField(name, unit, *read, metadata)};
        }
    }
    static constexpr FieldDefinition<BorrowedFieldAccess> makeScalar(
        const char* name, const char* unit, FieldType type, Read* read, Write* write) noexcept
    {
        if constexpr (writable) {
            return FieldDefinition<BorrowedFieldAccess>{
                materializeField(name, unit, type, *read, *write)};
        } else {
            return FieldDefinition<BorrowedFieldAccess>{
                materializeField(name, unit, type, *read)};
        }
    }
    static TELEMETRY_FORCE_INLINE Value read(const Field& entry) noexcept
    {
        static_assert(!nullableRead, "Slot reads must use the optional readSlot adapter");
        return (*FieldTableAccess::object<Read>(entry.get))();
    }
    template <class T>
    static TELEMETRY_FORCE_INLINE std::optional<T> readSlot(const Field& entry) noexcept
    {
        auto target = resolveFactoryCallable(FieldTableAccess::object<Read>(entry.get));
        if (!target) return std::nullopt;
        using Stored = Scalar::NativeType<Scalar::from(RawNumberT<Value>{}).type()>;
        return readNumber<T>(static_cast<Stored>(invokeResolvedCallable(target)));
    }
    static TELEMETRY_FORCE_INLINE WriteResult write(const Field& entry, Value value) noexcept
    {
        if constexpr (writable) {
            auto target = resolveFactoryCallable(FieldTableAccess::object<Write>(entry.set));
            if constexpr (isCallableSlot<Write>) {
                if (!target) return WriteResult::Unavailable;
            }
            return invokeResolvedCallable(target, value);
        }
        else return WriteResult::ReadOnly;
    }
};

struct ManualFieldAccess {
    using Value = Scalar;
    static constexpr FieldDefinition<ManualFieldAccess> make(Field entry) noexcept;
};

// Only factories pair an Access type with its matching immutable descriptor.
// That invariant permits reading the original payload directly on typed calls.
// Manual Scalar definitions deliberately retain checked erased dispatch.
template <class Access>
class FieldDefinition {
    friend Access;
    const Field entry_;
    constexpr explicit FieldDefinition(Field entry) noexcept : entry_(entry)
    {
        // Public factories publish complete descriptions. Low-level Field
        // descriptors may still represent invalid imported metadata for the
        // serializers to reject; do not let a factory silently publish it.
        if (entry.name == nullptr || entry.unit == nullptr) invalidFieldLimits();
    }
public:
    using Value = typename Access::Value;
    static constexpr bool hasNativeFastPath = isFactoryValue<Value>;
    constexpr Field materialize() const noexcept { return entry_; }

    // Replace only policy metadata, retaining the exact native Access type.
    // The Field constructor validates capability once; no dispatch path checks
    // these flags. Applying another policy replaces the previous mask.
    constexpr FieldDefinition withFlags(FieldFlags policy) const noexcept
    {
        return FieldDefinition{Field{entry_.name, entry_.unit, entry_.declaredType,
                                     entry_.get, entry_.set, policy}};
    }

    template <class T>
    static TELEMETRY_FORCE_INLINE std::optional<T> read(const Field& entry) noexcept
    {
        if constexpr (hasNativeFastPath) {
            // A weak NTTP or direct function pointer may resolve to null.
            // Check before Access::read(), whose native return has no absence tag.
            if constexpr (Access::checkReadTarget) {
                if (!Access::readTargetAvailable(entry)) return std::nullopt;
            }
            // Inferred fields use the getter's exact numeric representation;
            // canonical integer aliases change C++ spelling, not the value.
            using Raw = RawNumberT<Value>;
            using Stored = Scalar::NativeType<Scalar::from(Raw{}).type()>;
            if constexpr (Access::nullableRead) return Access::template readSlot<T>(entry);
            else {
                const Stored number = static_cast<Stored>(Access::read(entry));
                return readNumber<T>(number);
            }
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

constexpr FieldDefinition<ManualFieldAccess> ManualFieldAccess::make(Field entry) noexcept
{ return FieldDefinition<ManualFieldAccess>{entry}; }

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
    return Access::make(name, unit, std::addressof(owner), metadata);
}
template <auto Read, auto Write = nullptr, class Limits = detail::NoLimits,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write> && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Limits metadata = {}) noexcept
{
    using Access = detail::StaticFieldAccess<Read, Write, detail::NoOwner, Limits>;
    return Access::make(name, unit, nullptr, metadata);
}
template <auto Read, auto Write = nullptr, class Owner,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !std::is_reference_v<Owner>, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type, Owner& owner) noexcept
{
    using Access = detail::StaticFieldAccess<Read, Write, std::remove_reference_t<Owner>>;
    return Access::makeScalar(name, unit, type, std::addressof(owner));
}
template <auto Read, auto Write = nullptr,
          std::enable_if_t<!detail::fieldNeedsOwner<Read, Write>, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type) noexcept
{
    using Access = detail::StaticFieldAccess<Read, Write, detail::NoOwner>;
    return Access::makeScalar(name, unit, type, nullptr);
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
    return detail::DirectFieldAccess<Function>::make(
        name, unit, detail::fieldFunction<Function>(read), nullptr, metadata);
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
    return detail::DirectFieldAccess<R, W>::make(
        name, unit, detail::fieldFunction<R>(read), detail::fieldFunction<W>(write), metadata);
}
template <class Read, class Limits = detail::NoLimits,
          std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && detail::IsLimits<Limits>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, Read& read, Limits metadata = {}) noexcept
{
    return detail::BorrowedFieldAccess<Read, std::nullptr_t, Limits>::make(
        name, unit, std::addressof(read), nullptr, metadata);
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
    return detail::BorrowedFieldAccess<Read, Write, Limits>::make(
        name, unit, std::addressof(read), std::addressof(write), metadata);
}
template <class Read, std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && !detail::HasNativeFunctionPointer<Read>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type, Read& read) noexcept
{
    return detail::BorrowedFieldAccess<Read>::makeScalar(name, unit, type, std::addressof(read), nullptr);
}
template <class Read, class Write, std::enable_if_t<std::is_class_v<std::remove_cv_t<Read>>
              && std::is_class_v<std::remove_cv_t<Write>>
              && !detail::HasNativeFunctionPointer<Read>::value
              && !detail::HasNativeFunctionPointer<Write>::value
              && !detail::IsLimits<std::remove_cv_t<Write>>::value, int> = 0>
constexpr auto field(const char* name, const char* unit, FieldType type, Read& read, Write& write) noexcept
{
    return detail::BorrowedFieldAccess<Read, Write>::makeScalar(
        name, unit, type, std::addressof(read), std::addressof(write));
}

// Deduce the argument independently: an explicit const Owner must not permit
// a proxy conversion to a short-lived owner. Safe cv/base lvalues still bind.
template <auto Read, auto Write = nullptr, class Owner = void, class Limits = detail::NoLimits,
          class Argument,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !detail::isBorrowedObjectArgument<Owner, Argument>
              && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, Argument&&, Limits = {}) = delete;
template <auto Read, auto Write = nullptr, class Owner = void, class Argument,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !detail::isBorrowedObjectArgument<Owner, Argument>, int> = 0>
auto field(const char*, const char*, FieldType, Argument&&) = delete;

// An explicit Owner allows {} to create a temporary even though Argument
// cannot be deduced from braces. Proxy elements need their own list check.
template <auto Read, auto Write = nullptr, class Owner, class Limits = detail::NoLimits,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !std::is_reference_v<Owner> && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, std::remove_reference_t<Owner>&&, Limits = {}) = delete;
template <auto Read, auto Write = nullptr, class Owner, class Limits = detail::NoLimits, class Argument,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !detail::isBorrowedObjectArgument<Owner, Argument&>
              && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, std::initializer_list<Argument>, Limits = {}) = delete;
template <auto Read, auto Write = nullptr, class Owner,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !std::is_reference_v<Owner>, int> = 0>
auto field(const char*, const char*, FieldType, std::remove_reference_t<Owner>&&) = delete;
template <auto Read, auto Write = nullptr, class Owner, class Argument,
          std::enable_if_t<detail::fieldNeedsOwner<Read, Write>
              && !detail::isBorrowedObjectArgument<Owner, Argument&>, int> = 0>
auto field(const char*, const char*, FieldType, std::initializer_list<Argument>) = delete;

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

template <class Expected, class Argument>
using BorrowedArgumentType = std::conditional_t<std::is_void_v<Expected>,
    std::remove_reference_t<Argument>, Expected>;
} // namespace detail

template <class Read = void, class Limits = detail::NoLimits, class Argument,
          std::enable_if_t<detail::isBorrowedFieldCallable<detail::BorrowedArgumentType<Read, Argument>>
              && !detail::isBorrowedObjectArgument<Read, Argument>
              && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, Argument&&, Limits = {}) = delete;
template <class Read = void, class Write = void, class Limits = detail::NoLimits,
          class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, Limits>
              && !std::is_same_v<std::decay_t<ReadArgument>, ScalarType>
              && !std::is_same_v<std::decay_t<ReadArgument>, FieldType>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument>), int> = 0>
auto field(const char*, const char*, ReadArgument&&, WriteArgument&&, Limits = {}) = delete;

template <class Read = void, class Argument,
          std::enable_if_t<detail::isBorrowedFieldCallable<detail::BorrowedArgumentType<Read, Argument>>
              && !detail::isBorrowedObjectArgument<Read, Argument>, int> = 0>
auto field(const char*, const char*, FieldType, Argument&&) = delete;
template <class Read = void, class Write = void, class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, detail::NoLimits>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument>), int> = 0>
auto field(const char*, const char*, FieldType, ReadArgument&&, WriteArgument&&) = delete;

// Braced borrowed callables follow the same rule as explicit owners. The
// mixed pair forms check both arguments: a safe {getter} must not conceal a
// setter conversion, nor may a safe {setter} conceal a getter conversion.
template <class Read, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldCallable<Read>
              && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, std::remove_reference_t<Read>&&, Limits = {}) = delete;
template <class Read, class Limits = detail::NoLimits, class Argument,
          std::enable_if_t<detail::isBorrowedFieldCallable<Read>
              && !detail::isBorrowedObjectArgument<Read, Argument&>
              && detail::IsLimits<Limits>::value, int> = 0>
auto field(const char*, const char*, std::initializer_list<Argument>, Limits = {}) = delete;
template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, Limits>, int> = 0>
auto field(const char*, const char*, std::remove_reference_t<Read>&&, Write&, Limits = {}) = delete;
template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, Limits>, int> = 0>
auto field(const char*, const char*, Read&, std::remove_reference_t<Write>&&, Limits = {}) = delete;
template <class Read, class Write, class Limits = detail::NoLimits,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, Limits>, int> = 0>
auto field(const char*, const char*, std::remove_reference_t<Read>&&, std::remove_reference_t<Write>&&, Limits = {}) = delete;
template <class Read = void, class Write = void, class Limits = detail::NoLimits, class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, Limits>
              && !std::is_same_v<std::decay_t<ReadArgument>, ScalarType>
              && !std::is_same_v<std::decay_t<ReadArgument>, FieldType>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument&>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument>), int> = 0>
auto field(const char*, const char*, std::initializer_list<ReadArgument>, WriteArgument&&, Limits = {}) = delete;
template <class Read = void, class Write = void, class Limits = detail::NoLimits, class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, Limits>
              && !std::is_same_v<std::decay_t<ReadArgument>, ScalarType>
              && !std::is_same_v<std::decay_t<ReadArgument>, FieldType>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument&>), int> = 0>
auto field(const char*, const char*, ReadArgument&&, std::initializer_list<WriteArgument>, Limits = {}) = delete;
template <class Read = void, class Write = void, class Limits = detail::NoLimits, class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, Limits>
              && !std::is_same_v<std::decay_t<ReadArgument>, ScalarType>
              && !std::is_same_v<std::decay_t<ReadArgument>, FieldType>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument&>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument&>), int> = 0>
auto field(const char*, const char*, std::initializer_list<ReadArgument>, std::initializer_list<WriteArgument>, Limits = {}) = delete;

template <class Read,
          std::enable_if_t<detail::isBorrowedFieldCallable<Read>, int> = 0>
auto field(const char*, const char*, FieldType, std::remove_reference_t<Read>&&) = delete;
template <class Read, class Argument,
          std::enable_if_t<detail::isBorrowedFieldCallable<Read>
              && !detail::isBorrowedObjectArgument<Read, Argument&>, int> = 0>
auto field(const char*, const char*, FieldType, std::initializer_list<Argument>) = delete;
template <class Read, class Write,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, detail::NoLimits>, int> = 0>
auto field(const char*, const char*, FieldType, std::remove_reference_t<Read>&&, Write&) = delete;
template <class Read, class Write,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, detail::NoLimits>, int> = 0>
auto field(const char*, const char*, FieldType, Read&, std::remove_reference_t<Write>&&) = delete;
template <class Read, class Write,
          std::enable_if_t<detail::isBorrowedFieldPair<Read, Write, detail::NoLimits>, int> = 0>
auto field(const char*, const char*, FieldType, std::remove_reference_t<Read>&&, std::remove_reference_t<Write>&&) = delete;
template <class Read = void, class Write = void, class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, detail::NoLimits>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument&>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument>), int> = 0>
auto field(const char*, const char*, FieldType, std::initializer_list<ReadArgument>, WriteArgument&&) = delete;
template <class Read = void, class Write = void, class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, detail::NoLimits>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument&>), int> = 0>
auto field(const char*, const char*, FieldType, ReadArgument&&, std::initializer_list<WriteArgument>) = delete;
template <class Read = void, class Write = void, class ReadArgument, class WriteArgument,
          std::enable_if_t<detail::isBorrowedFieldPair<
              detail::BorrowedArgumentType<Read, ReadArgument>,
              detail::BorrowedArgumentType<Write, WriteArgument>, detail::NoLimits>
              && (!detail::isBorrowedObjectArgument<Read, ReadArgument&>
                  || !detail::isBorrowedObjectArgument<Write, WriteArgument&>), int> = 0>
auto field(const char*, const char*, FieldType, std::initializer_list<ReadArgument>, std::initializer_list<WriteArgument>) = delete;


constexpr auto field(Field entry) noexcept
{ return detail::ManualFieldAccess::make(entry); }
constexpr auto reservedField() noexcept { return field(Field{}); }

} // namespace telemetry
#endif
