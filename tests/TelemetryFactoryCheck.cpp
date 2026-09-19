// Inferred definitions must preserve the existing concrete Field contract.
#include "Telemetry.h"
#include <cstdio>
#include <limits>
#include <type_traits>

namespace {
using namespace telemetry;
int checks = 0, failures = 0;
void expect(bool ok, const char* label) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", label); } }
enum class Mode : std::uint8_t { Off, Automatic, Manual };
enum Legacy { Low = -2, High = 2 };
struct Device {
    float voltage = 230.0f;
    Mode modeValue = Mode::Automatic;
    Legacy legacy = Low;
    int writes = 0;
    float get() const noexcept { return voltage; }
    WriteResult set(float value) noexcept { ++writes; voltage = value; return WriteResult::Applied; }
    Mode mode() const noexcept { return modeValue; }
    WriteResult setMode(Mode value) noexcept { ++writes; modeValue = value; return WriteResult::Applied; }
    Scalar scalar() const noexcept { return voltage; }
    WriteResult setScalar(const Scalar& value) noexcept { voltage = value.get<float>(); return WriteResult::Applied; }
    Legacy readLegacy() const noexcept { return legacy; }
    WriteResult writeLegacy(Legacy value) noexcept { legacy = value; return WriteResult::Applied; }
    float refGet() const & noexcept { return voltage; }
};
Device device;
float globalValue = 1.0f;
float getFree() noexcept { return globalValue; }
WriteResult setFree(float value) noexcept { globalValue = value; return WriteResult::Busy; }
struct System { static std::uint64_t uptime() noexcept { return UINT64_MAX; } };
constexpr auto field = makeField<&Device::get, &Device::set>(0, "Voltage", "V", device, limits(250.0f, 1.0f, 1000.0f));
constexpr auto enumField = makeField<&Device::mode, &Device::setMode>(1, "Mode", "", device);
constexpr auto readOnly = makeField<&Device::get>(2, "RO", "V", device);
constexpr auto global = makeField<&getFree, &setFree>(3, "Free", "");
constexpr auto functionName = makeField(3, "Free", "", getFree);
constexpr auto functionAddress = makeField(3, "Free", "", &getFree);
constexpr auto clockField = makeField<&System::uptime>(4, "Clock", "s");
constexpr auto explicitType = makeField<&Device::scalar, &Device::setScalar>(5, "Scalar", "", ScalarType::F32, device);
constexpr auto legacyField = makeField<&Device::readLegacy, &Device::writeLegacy>(6, "Legacy", "", device);
constexpr auto lambdaField = makeField(7, "lambda", "", []() noexcept { return globalValue; });
constexpr auto plusField = makeField(8, "plus", "", +[]() noexcept { return globalValue; });
constexpr auto lambdaGet = +[]() noexcept { return globalValue; };
constexpr auto lambdaSet = +[](float value) noexcept { globalValue=value;return WriteResult::Applied; };
constexpr auto lambdaPair = makeField<lambdaGet,lambdaSet>(9,"pair","",limits(1.0f,0.0f,5.0f));
constexpr auto lambdaEnum = +[]() noexcept { return Mode::Automatic; };
constexpr auto lambdaEnumField = makeField<lambdaEnum>(10,"enum","",limits(Mode::Manual));
static_assert(std::is_same_v<std::remove_cv_t<decltype(field)>, Field>);
static_assert(field.declaredType == ScalarType::F32 && field.declaredType.defaultValue().get<float>() == 250.0f);
static_assert(enumField.declaredType == ScalarType::U8 && enumField.declaredType.hasEnum());
static_assert(clockField.declaredType == ScalarType::U64 && !clockField.set);
static_assert(!std::is_copy_assignable_v<Field> && std::is_trivially_copyable_v<Field>);
static_assert(sizeof(decltype(limits(1.0f))) == sizeof(float));
}
int main()
{
    expect(field.read<float>() == 230.0f, "inferred member read");
    expect(field.write(300) == WriteResult::Applied && device.voltage == 300.0f, "native setter receives converted value");
    const auto writes = device.writes;
    expect(field.write(0) == WriteResult::InvalidValue && device.writes == writes, "limits block owner call");
    expect(field.write(std::numeric_limits<float>::infinity()) == WriteResult::InvalidValue, "nonfinite writes rejected");
    device.voltage = 2000.0f;
    expect(field.read<float>() == 2000.0f, "read ignores write limits");
    expect(readOnly.write(1) == WriteResult::ReadOnly, "read only factory");
    expect(enumField.read<std::uint8_t>() == 1, "enum getter produces underlying Scalar");
    expect(enumField.write(2) == WriteResult::Applied && device.modeValue == Mode::Manual, "enum setter receives semantic type");
    expect(enumField.write(3) == WriteResult::InvalidValue, "enum bounds preserved");
    expect(enumField.set(Scalar::fromU16(255)) == WriteResult::InvalidValue, "direct enum setter rejects wrong alternative");
    expect(field.set(Scalar::fromU16(12)) == WriteResult::InvalidValue, "direct typed setter rejects wrong alternative");
    expect(global.write(2.0f) == WriteResult::Busy && globalValue == 2.0f, "free setter result preserved");
    expect(global.read<float>() == 2.0f && functionName.read<float>() == 2.0f
           && functionAddress.read<float>() == 2.0f, "free getter: template, name and address forms");
    expect(clockField.read<std::uint64_t>() == UINT64_MAX, "static getter exact U64");
    expect(lambdaField.read<float>() == 2.0f && plusField.read<float>() == 2.0f, "bare and plus inline lambdas infer type");
    expect(lambdaPair.write(4) == WriteResult::Applied && globalValue == 4.0f, "C++17 named lambda pair");
    expect(lambdaPair.write(6) == WriteResult::InvalidValue, "named lambda pair limits");
    expect(lambdaEnumField.read<std::uint8_t>() == 1 && lambdaEnumField.declaredType.hasEnum(), "named enum lambda");
    expect(explicitType.write(10) == WriteResult::Applied && explicitType.read<float>() == 10.0f, "Scalar explicit escape hatch");
    expect(legacyField.write(0) == WriteResult::Applied && device.legacy == 0, "unnamed enum gap remains numeric");
    expect(legacyField.set(Scalar::fromS32(1000)) == WriteResult::InvalidValue, "unfixed enum out of range not cast");
    const Device constant{};
    const auto constantField = makeField<&Device::get>(0,"const","",constant);
    expect(constantField.read<float>() == 230.0f, "const lvalue owner");
    Device local;
    auto localField = makeField<&Device::get,&Device::set>(0,"local","",local);
    const auto copy = localField;
    expect(copy.write(50) == WriteResult::Applied && local.voltage == 50.0f, "copy retains borrowed owner");
    const auto refField = makeField<&Device::refGet>(0,"ref","",local);
    expect(refField.read<float>() == 50.0f, "const lvalue-qualified method");
    const Field rows[] = {makeField<&Device::get>(0,"v","",local),makeField<&Device::mode>(1,"m","",local)};
    const Catalog catalogs[] = {{0,"factory",rows}};
    const CatalogIndex index{catalogs};
    expect(index.read<float>(0) == 50.0f && index.read<std::uint8_t>(1) == 1, "same CatalogIndex consumes factory fields");
    std::printf("%d/%d factory checks passed\n", checks-failures,checks);
    return failures != 0;
}
