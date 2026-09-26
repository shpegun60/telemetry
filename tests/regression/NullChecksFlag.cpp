// Regression promoted from tests/review/slots/NullChecksFlag.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// GCC 13/14 may leave function addresses incomparable with nullptr in constant
// evaluation under -fno-delete-null-pointer-checks (also implied by UBSan).
// Public NTTP targets require exact constant nonnull-address validation: a
// named GNU weak function may resolve to null. Private library adapter thunks
// use a known-defined construction path without another nullable conversion.
//
// This is a targeted control, not general support for the flag: arbitrary
// nullable constexpr pointer parameters, public function NTTP targets,
// and constexpr Persistent validation (Getter/Setter::operator bool) still
// meet the affected GCC boundary. The runtime checks below preserve exact null
// behavior instead of guessing that
// an expression which GCC cannot fold is nonnull. GNU unresolved weak symbols
// are exercised only as runtime pointer values; NTTP targets require a constant
// nonnull address. Default mode uses normal flags; success of this one file
// does not establish full-suite GCC UBSan compatibility.
// Supported subset: -DTELEMETRY_NULL_CHECKS_SLOT_ONLY
//                  -fno-delete-null-pointer-checks -fsyntax-only <this>
#include "Telemetry.h"
#include <cstdio>
using namespace telemetry;

#if defined(TELEMETRY_NULL_CHECKS_SLOT_ONLY)
FunctionSlot<float() noexcept> reading;
FunctionSlot<WriteResult(float) noexcept> writing;
constexpr FieldTable rows{
    field("Slot", "V", reading),
    field("Slot pair", "V", reading, writing),
};
int main()
{
    return !rows.read<0>() && rows.write<1>(2.f) == WriteResult::Unavailable ? 0 : 1;
}
#else
namespace null_checks_probe {
float nativeValue = 1.f;
unsigned writeCalls = 0;
float readNative() noexcept { return nativeValue; }
WriteResult writeNative(float next) noexcept
{ nativeValue = next; ++writeCalls; return WriteResult::Applied; }
Scalar readScalar() noexcept { return Scalar::from(nativeValue); }
WriteResult writeScalar(const Scalar& next) noexcept
{ return writeNative(next.get<float>()); }
enum class Mode : unsigned { Off, On };
Mode readMode() noexcept { return Mode::On; }

// Persistent metadata inspects the presence of generated binding thunks.
struct Owner {
    float value = 10.f;
    float read() const noexcept;
    WriteResult write(float next) noexcept;
};
float Owner::read() const noexcept { return value; }
WriteResult Owner::write(float next) noexcept { value = next; return WriteResult::Applied; }

// A template specialization's address is the original failing free-getter
// shape. The enum factory has another generated free-function adapter.
#if defined(REVIEW_TEMPLATE_CONSTEXPR_TARGET)
template <class T> T readTemplate() noexcept { return T{7}; }
constexpr auto templateTarget = Getter::bind<&readTemplate<float>>();
#endif

using NativeRead = float (*)() noexcept;
using NativeWrite = WriteResult (*)(float) noexcept;
using ScalarRead = Scalar (*)() noexcept;
using ScalarWrite = WriteResult (*)(const Scalar&) noexcept;
constexpr Getter noGetter{static_cast<NativeRead>(nullptr)};
constexpr Setter noSetter{static_cast<NativeWrite>(nullptr)};
static_assert(!noGetter && !noSetter);
static_assert(sizeof(Getter) == 2 * sizeof(void*) && sizeof(Setter) == 2 * sizeof(void*));
}
using namespace null_checks_probe;

#if defined(__GNUC__) && defined(__ELF__)
float missingNativeRead() noexcept __attribute__((weak));
WriteResult missingNativeWrite(float) noexcept __attribute__((weak));
Scalar missingScalarRead() noexcept __attribute__((weak));
WriteResult missingScalarWrite(const Scalar&) noexcept __attribute__((weak));
__attribute__((weak)) float definedWeakRead() noexcept { return 19.f; }
#endif

FunctionSlot<float() noexcept> reading;
FunctionSlot<WriteResult(float) noexcept> writing;
constexpr FieldTable rows{
    field("Slot", "V", reading),
    field("Slot pair", "V", reading, writing),
    field<&readNative>("Free", "V"),
    field<&readNative, &writeNative>("Free pair", "V"),
    field<&readMode>("Enum", ""),
    field<&readScalar, &writeScalar>("Scalar pair", "V", ScalarType::F32),
};

#if defined(REVIEW_SLOT_CONSTEXPR_BIND)
float source() noexcept { return 1.f; }
constexpr bool boundInConstantEvaluation() noexcept
{
    FunctionSlot<float() noexcept> local;
    local.bind(&source);
    return local.available(); // function_ != nullptr on a function address
}
static_assert(boundInConstantEvaluation());
#endif

int main()
{
    bool ok = true;
    unsigned checks = 0;
    const auto check = [&](bool result) noexcept { ++checks; ok = ok && result; };
    check(!rows.read<0>());
    check(rows.write<1>(2.f) == WriteResult::Unavailable);
    check(rows.read<2>() == 1.f);
    check(rows.write<3>(3.f) == WriteResult::Applied && rows.read<3>() == 3.f);
    check(rows.read<4, unsigned>() == 1u);
    check(rows.write<5>(4.f) == WriteResult::Applied && rows.read<5, float>() == 4.f);
    check(rows[4].read<unsigned>() == 1u);
    reading.bind(&readNative);
    writing.bind(&writeNative);
    check(rows.read<0>() == 4.f);
    check(rows.write<1>(5.f) == WriteResult::Applied && rows.read<1>() == 5.f);

    // Volatile pointer objects force these paths to consume runtime values.
    NativeRead volatile nativeRead = &readNative;
    NativeWrite volatile nativeWrite = &writeNative;
    ScalarRead volatile scalarRead = &readScalar;
    ScalarWrite volatile scalarWrite = &writeScalar;
    const Getter nativeGetter{nativeRead};
    const Setter nativeSetter{nativeWrite};
    const Getter scalarGetter{scalarRead};
    const Setter scalarSetter{scalarWrite};
    check(bool(nativeGetter) && nativeGetter().get<float>() == 5.f);
    check(bool(nativeSetter) && nativeSetter(Scalar::from(6.f)) == WriteResult::Applied);
    check(bool(scalarGetter) && scalarGetter().get<float>() == 6.f);
    check(bool(scalarSetter) && scalarSetter(Scalar::from(8.f)) == WriteResult::Applied);
    check(nativeGetter().get<float>() == 8.f);
    nativeRead = nullptr;
    nativeWrite = nullptr;
    scalarRead = nullptr;
    scalarWrite = nullptr;
    const Getter emptyNativeGetter{nativeRead};
    const Setter emptyNativeSetter{nativeWrite};
    const Getter emptyScalarGetter{scalarRead};
    const Setter emptyScalarSetter{scalarWrite};
    const unsigned before = writeCalls;
    check(!emptyNativeGetter && emptyNativeGetter().type() == ScalarType::Null);
    check(!emptyNativeSetter && emptyNativeSetter(Scalar::from(9.f)) == WriteResult::ReadOnly);
    check(!emptyScalarGetter && emptyScalarGetter().type() == ScalarType::Null);
    check(!emptyScalarSetter && emptyScalarSetter(Scalar::from(9.f)) == WriteResult::ReadOnly);
    check(writeCalls == before && nativeValue == 8.f);
    check(noGetter().type() == ScalarType::Null && noSetter(Scalar::from(9.f)) == WriteResult::ReadOnly);

    Owner owner;
    const FieldTable persistent{
        field<&Owner::read, &Owner::write>("Persistent", "V", owner).withFlags(FieldFlag::Persistent),
    };
    check(persistent[0].persistent() && persistent[0].writable());
    check(persistent.write<0>(11.f) == WriteResult::Applied && persistent.read<0>() == 11.f);

#if defined(__GNUC__) && defined(__ELF__)
    nativeRead = &missingNativeRead;
    nativeWrite = &missingNativeWrite;
    scalarRead = &missingScalarRead;
    scalarWrite = &missingScalarWrite;
    const Getter weakNativeGetter{nativeRead};
    const Setter weakNativeSetter{nativeWrite};
    const Getter weakScalarGetter{scalarRead};
    const Setter weakScalarSetter{scalarWrite};
    check(!weakNativeGetter && weakNativeGetter().type() == ScalarType::Null);
    check(!weakNativeSetter && weakNativeSetter(Scalar::from(9.f)) == WriteResult::ReadOnly);
    check(!weakScalarGetter && weakScalarGetter().type() == ScalarType::Null);
    check(!weakScalarSetter && weakScalarSetter(Scalar::from(9.f)) == WriteResult::ReadOnly);
    nativeRead = &definedWeakRead;
    const Getter definedWeakGetter{nativeRead};
    check(bool(definedWeakGetter) && definedWeakGetter().get<float>() == 19.f);
#endif
    std::printf("%s %u null-check flag and runtime pointer checks passed\n", ok ? "All" : "Not all", checks);
    return ok ? 0 : 1;
}
#endif
