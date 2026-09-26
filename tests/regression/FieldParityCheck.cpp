// Regression promoted from tests/review/fields/NativeDynamicParity.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review probe (fields slice): the native FieldTable::write<I>/read<I> fast
// paths must agree with the dynamic Field::write/read contract for every
// binding form (member NTTP, free NTTP, parameter function pointers, borrowed
// closures, FunctionSlot, OwnerSlot), with and without limits, for every
// supported value type and a boundary-heavy input set.
#include "Telemetry.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace telemetry;

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* what, const char* type, int form, int input, int limit)
{
    ++checks;
    if (!ok && ++failures <= 40)
        std::printf("MISMATCH %s type=%s form=%d input=%d limits=%d\n", what, type, form, input, limit);
}

template <class T> struct Store {
    static inline T value{};
    static inline int writes = 0;
    static inline int reads = 0;
};
template <class T> T freeRead() noexcept { ++Store<T>::reads; return Store<T>::value; }
template <class T> WriteResult freeWrite(T v) noexcept { ++Store<T>::writes; Store<T>::value = v; return WriteResult::Applied; }
template <class T> struct Member {
    T read() const noexcept { ++Store<T>::reads; return Store<T>::value; }
    WriteResult write(T v) noexcept { ++Store<T>::writes; Store<T>::value = v; return WriteResult::Applied; }
};
template <class T> Member<T> member;
template <class T> OwnerSlot<Member<T>> ownerSlot;
template <class T> FunctionSlot<T() noexcept> readSlot;
template <class T> FunctionSlot<WriteResult(T) noexcept> writeSlot;
template <class T> struct ClosureRead { int tag = 0; T operator()() const noexcept { return freeRead<T>(); } };
template <class T> struct ClosureWrite { int tag = 0; WriteResult operator()(T v) const noexcept { return freeWrite<T>(v); } };
template <class T> const ClosureRead<T> closureRead{};
template <class T> const ClosureWrite<T> closureWrite{};

template <class T> constexpr auto boundedLimits()
{
    if constexpr (std::is_same_v<T, bool>) return limits(true, true, true);
    else if constexpr (std::is_floating_point_v<T>) return limits(T(1), T(-5.5), T(10.25));
    else if constexpr (std::is_signed_v<T>) return limits(T(1), T(-5), T(10));
    else return limits(T(2), T(2), T(10));
}

template <class T, int Form, bool Bounded>
auto makeDefinition()
{
    if constexpr (Bounded) {
        constexpr auto lim = boundedLimits<T>();
        if constexpr (Form == 0) return field<&Member<T>::read, &Member<T>::write>("m", "", member<T>, lim);
        else if constexpr (Form == 1) return field<&freeRead<T>, &freeWrite<T>>("f", "", lim);
        else if constexpr (Form == 2) return field("p", "", &freeRead<T>, &freeWrite<T>, lim);
        else if constexpr (Form == 3) return field("c", "", closureRead<T>, closureWrite<T>, lim);
        else if constexpr (Form == 4) return field("s", "", readSlot<T>, writeSlot<T>, lim);
        else return field<&Member<T>::read, &Member<T>::write>("o", "", ownerSlot<T>, lim);
    } else {
        if constexpr (Form == 0) return field<&Member<T>::read, &Member<T>::write>("m", "", member<T>);
        else if constexpr (Form == 1) return field<&freeRead<T>, &freeWrite<T>>("f", "");
        else if constexpr (Form == 2) return field("p", "", &freeRead<T>, &freeWrite<T>);
        else if constexpr (Form == 3) return field("c", "", closureRead<T>, closureWrite<T>);
        else if constexpr (Form == 4) return field("s", "", readSlot<T>, writeSlot<T>);
        else return field<&Member<T>::read, &Member<T>::write>("o", "", ownerSlot<T>);
    }
}

bool sameScalar(const Scalar& a, const Scalar& b)
{
    if (a.type() != b.type()) return false;
    if (a.type() == ScalarType::Null) return true;
    const auto x = convertScalar<double>(a), y = convertScalar<double>(b);
    if (!x || !y) return !x && !y;
    if (std::isnan(*x)) return std::isnan(*y);
    // exact comparison in double is exact for every non-64-bit type; compare
    // 64-bit integers exactly as well
    if (a.type() == ScalarType::U64) return a.get<std::uint64_t>() == b.get<std::uint64_t>();
    if (a.type() == ScalarType::S64) return a.get<std::int64_t>() == b.get<std::int64_t>();
    return std::memcmp(&*x, &*y, sizeof(double)) == 0;
}

template <class T, class R, class Table>
void compareRead(const Table& table, const char* name, int form, int input, int limit)
{
    const int before = Store<T>::reads;
    const auto native = table.template read<0, R>();
    const int nativeReads = Store<T>::reads - before;
    const auto dynamic = table[0].template read<R>();
    const int dynamicReads = Store<T>::reads - before - nativeReads;
    bool ok = native.has_value() == dynamic.has_value() && nativeReads == 1 && dynamicReads == 1;
    if (ok && native) {
        if constexpr (std::is_floating_point_v<R>)
            ok = (std::isnan(*native) && std::isnan(*dynamic))
                 || std::memcmp(&*native, &*dynamic, sizeof(R)) == 0;
        else ok = *native == *dynamic;
    }
    expect(ok, "read", name, form, input, limit);
}

template <class T, int Form, bool Bounded, class Input>
void compareWrite(const char* name, Input input, int inputIndex)
{
    const FieldTable table{makeDefinition<T, Form, Bounded>()};
    const Field& entry = table[0];
    Store<T>::value = T{};
    Store<T>::writes = 0;
    const WriteResult native = table.template write<0>(input);
    const T nativeValue = Store<T>::value;
    const int nativeWrites = Store<T>::writes;
    Store<T>::value = T{};
    Store<T>::writes = 0;
    const WriteResult dynamic = entry.write(input);
    const T dynamicValue = Store<T>::value;
    const int dynamicWrites = Store<T>::writes;
    bool ok = native == dynamic && nativeWrites == dynamicWrites;
    if (ok && native == WriteResult::Applied) {
        ok = std::memcmp(&nativeValue, &dynamicValue, sizeof(T)) == 0
             || (std::is_floating_point_v<T> && nativeValue == dynamicValue);
    }
    expect(ok, "write", name, Form, inputIndex, Bounded);
    // Keep a representative value in the source and compare typed reads.
    Store<T>::value = dynamicWrites ? dynamicValue : T{};
    compareRead<T, double>(table, name, Form, inputIndex, Bounded);
    compareRead<T, float>(table, name, Form, inputIndex, Bounded);
    compareRead<T, std::int8_t>(table, name, Form, inputIndex, Bounded);
    compareRead<T, std::uint64_t>(table, name, Form, inputIndex, Bounded);
    compareRead<T, std::int64_t>(table, name, Form, inputIndex, Bounded);
    compareRead<T, bool>(table, name, Form, inputIndex, Bounded);
    compareRead<T, T>(table, name, Form, inputIndex, Bounded);
    // Native inferred read must equal the dynamic Scalar read.
    const auto inferred = table.template read<0>();
    const Scalar scalar = entry.read();
    bool sameInferred = inferred.has_value() == (scalar.type() != ScalarType::Null);
    if (sameInferred && inferred) sameInferred = sameScalar(Scalar::from(*inferred), scalar);
    expect(sameInferred, "inferred", name, Form, inputIndex, Bounded);
}

template <class T, int Form, bool Bounded>
void inputs(const char* name)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    int i = 0;
    compareWrite<T, Form, Bounded>(name, 0, i++);
    compareWrite<T, Form, Bounded>(name, 1, i++);
    compareWrite<T, Form, Bounded>(name, -1, i++);
    compareWrite<T, Form, Bounded>(name, 12.7, i++);
    compareWrite<T, Form, Bounded>(name, -0.9, i++);
    compareWrite<T, Form, Bounded>(name, -0.0, i++);
    compareWrite<T, Form, Bounded>(name, 10.25f, i++);
    compareWrite<T, Form, Bounded>(name, 10.250001, i++);
    compareWrite<T, Form, Bounded>(name, -5.5, i++);
    compareWrite<T, Form, Bounded>(name, std::numeric_limits<std::int64_t>::min(), i++);
    compareWrite<T, Form, Bounded>(name, std::numeric_limits<std::uint64_t>::max(), i++);
    compareWrite<T, Form, Bounded>(name, std::numeric_limits<float>::max(), i++);
    compareWrite<T, Form, Bounded>(name, std::numeric_limits<double>::max(), i++);
    compareWrite<T, Form, Bounded>(name, std::numeric_limits<double>::denorm_min(), i++);
    compareWrite<T, Form, Bounded>(name, inf, i++);
    compareWrite<T, Form, Bounded>(name, -inf, i++);
    compareWrite<T, Form, Bounded>(name, nan, i++);
    compareWrite<T, Form, Bounded>(name, std::numeric_limits<float>::quiet_NaN(), i++);
    compareWrite<T, Form, Bounded>(name, true, i++);
    compareWrite<T, Form, Bounded>(name, false, i++);
    compareWrite<T, Form, Bounded>(name, 255u, i++);
    compareWrite<T, Form, Bounded>(name, 256, i++);
    compareWrite<T, Form, Bounded>(name, -128, i++);
    compareWrite<T, Form, Bounded>(name, -129, i++);
    compareWrite<T, Form, Bounded>(name, 65536u, i++);
    compareWrite<T, Form, Bounded>(name, 2147483648.0, i++);
    compareWrite<T, Form, Bounded>(name, 4294967296.0f, i++);
    compareWrite<T, Form, Bounded>(name, std::uint64_t{9007199254740993u}, i++);
    compareWrite<T, Form, Bounded>(name, 0x1p63, i++);
    compareWrite<T, Form, Bounded>(name, 0x1p64f, i++);
    compareWrite<T, Form, Bounded>(name, char{'A'}, i++);
    compareWrite<T, Form, Bounded>(name, wchar_t{65}, i++);
    compareWrite<T, Form, Bounded>(name, char16_t{65535}, i++);
    compareWrite<T, Form, Bounded>(name, char32_t{0xFFFFFFFFu}, i++);
    compareWrite<T, Form, Bounded>(name, Scalar::fromU16(12), i++);
    compareWrite<T, Form, Bounded>(name, Scalar::fromS64(-3), i++);
    compareWrite<T, Form, Bounded>(name, Scalar::fromF32(std::numeric_limits<float>::infinity()), i++);
    compareWrite<T, Form, Bounded>(name, Scalar::fromF64(nan), i++);
    compareWrite<T, Form, Bounded>(name, Scalar::fromBool(true), i++);
    compareWrite<T, Form, Bounded>(name, Scalar::null(), i++);
}

template <class T>
void allForms(const char* name)
{
    member<T> = {};
    ownerSlot<T>.bind(member<T>);
    readSlot<T>.bind(&freeRead<T>);
    writeSlot<T>.bind(&freeWrite<T>);
    inputs<T, 0, false>(name); inputs<T, 0, true>(name);
    inputs<T, 1, false>(name); inputs<T, 1, true>(name);
    inputs<T, 2, false>(name); inputs<T, 2, true>(name);
    inputs<T, 3, false>(name); inputs<T, 3, true>(name);
    inputs<T, 4, false>(name); inputs<T, 4, true>(name);
    inputs<T, 5, false>(name); inputs<T, 5, true>(name);
}
} // namespace

// -DPARITY_GROUP=1..6 builds three types per translation unit (sanitizer
// builds of all 18 types at once need about 15 GB of compiler memory).
#ifndef PARITY_GROUP
#define PARITY_GROUP 0
#endif
#define PARITY_IN(g) (PARITY_GROUP == 0 || PARITY_GROUP == (g))
int main()
{
#if PARITY_IN(1)
    allForms<float>("float"); allForms<double>("double"); allForms<bool>("bool");
#endif
#if PARITY_IN(2)
    allForms<std::uint8_t>("u8"); allForms<std::uint16_t>("u16"); allForms<std::uint32_t>("u32");
#endif
#if PARITY_IN(3)
    allForms<std::uint64_t>("u64"); allForms<std::int8_t>("s8"); allForms<std::int16_t>("s16");
#endif
#if PARITY_IN(4)
    allForms<std::int32_t>("s32"); allForms<std::int64_t>("s64"); allForms<char>("char");
#endif
#if PARITY_IN(5)
    allForms<wchar_t>("wchar_t"); allForms<long>("long"); allForms<unsigned long>("unsigned long");
#endif
#if PARITY_IN(6)
    allForms<long long>("long long"); allForms<char16_t>("char16_t"); allForms<char32_t>("char32_t");
#endif
    std::printf("%d/%d native/dynamic parity checks passed\n", checks - failures, checks);
    return failures != 0;
}
