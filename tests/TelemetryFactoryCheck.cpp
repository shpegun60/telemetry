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
enum class Error : std::uint16_t { None = 0, Overvoltage = 1000, Overcurrent = 2000 };
enum class CharCode : char {
    Low = std::numeric_limits<char>::is_signed ? -3 : 3,
    High = 7
};
enum Legacy { Low = -2, High = 2 };
struct Device {
    float voltage = 230.0f;
    Mode modeValue = Mode::Automatic;
    Legacy legacy = Low;
    Error errorValue = Error::None;
    int writes = 0;
    float get() const noexcept { return voltage; }
    WriteResult set(float value) noexcept { ++writes; voltage = value; return WriteResult::Applied; }
    Mode mode() const noexcept { return modeValue; }
    WriteResult setMode(Mode value) noexcept { ++writes; modeValue = value; return WriteResult::Applied; }
    Scalar scalar() const noexcept { return voltage; }
    WriteResult setScalar(const Scalar& value) noexcept { voltage = value.get<float>(); return WriteResult::Applied; }
    Legacy readLegacy() const noexcept { return legacy; }
    WriteResult writeLegacy(Legacy value) noexcept { legacy = value; return WriteResult::Applied; }
    Error error() const noexcept { return errorValue; }
    WriteResult setError(Error value) noexcept { errorValue = value; return WriteResult::Applied; }
    float refGet() const & noexcept { return voltage; }
};
Device device;
float globalValue = 1.0f;
float getFree() noexcept { return globalValue; }
WriteResult setFree(float value) noexcept { globalValue = value; return WriteResult::Busy; }
struct System { static std::uint64_t uptime() noexcept { return UINT64_MAX; } };
constexpr auto testedField = telemetry::field<&Device::get, &Device::set>("Voltage", "V", device, limits(250.0f, 1.0f, 1000.0f)).materialize();
constexpr auto enumField = telemetry::field<&Device::mode, &Device::setMode>("Mode", "", device).materialize();
constexpr auto readOnly = telemetry::field<&Device::get>("RO", "V", device).materialize();
constexpr auto global = telemetry::field<&getFree, &setFree>("Free", "").materialize();
constexpr auto functionName = telemetry::field("Free", "", getFree).materialize();
constexpr auto functionAddress = telemetry::field("Free", "", &getFree).materialize();
constexpr auto clockField = telemetry::field<&System::uptime>("Clock", "s").materialize();
constexpr auto explicitType = telemetry::field<&Device::scalar, &Device::setScalar>("Scalar", "", ScalarType::F32, device).materialize();
constexpr auto legacyField = telemetry::field<&Device::readLegacy, &Device::writeLegacy>("Legacy", "", device).materialize();
constexpr auto lambdaField = telemetry::field("lambda", "", []() noexcept { return globalValue; }).materialize();
constexpr auto plusField = telemetry::field("plus", "", +[]() noexcept { return globalValue; }).materialize();
constexpr auto lambdaGet = +[]() noexcept { return globalValue; };
constexpr auto lambdaSet = +[](float value) noexcept { globalValue=value;return WriteResult::Applied; };
constexpr auto lambdaPair = telemetry::field<lambdaGet,lambdaSet>("pair","",limits(1.0f,0.0f,5.0f)).materialize();
constexpr auto lambdaEnum = +[]() noexcept { return Mode::Automatic; };
constexpr auto lambdaEnumField = telemetry::field<lambdaEnum>("enum","",limits(Mode::Manual)).materialize();
constexpr auto parameterPair = telemetry::field("parameter pair","",getFree,setFree).materialize();
constexpr auto sparseField = telemetry::field<&Device::error, &Device::setError>("Error", "", device,
    enumSpec<Error::None, Error::Overvoltage, Error::Overcurrent>(Error::Overvoltage)).materialize();
constexpr auto charCodeSpec = enumSpec<CharCode::High, CharCode::Low>();
static_assert(std::is_same_v<std::remove_cv_t<decltype(testedField)>, Field>);
static_assert(testedField.declaredType == ScalarType::F32 && testedField.declaredType.defaultValue().get<float>() == 250.0f);
static_assert(enumField.declaredType == ScalarType::U8 && enumField.declaredType.hasEnum());
static_assert(clockField.declaredType == ScalarType::U64 && !clockField.set);
static_assert(static_cast<bool>(parameterPair.get) && static_cast<bool>(parameterPair.set));
static_assert(sparseField.declaredType == ScalarType::U16
              && sparseField.declaredType.hasEnum()
              && sparseField.declaredType.minimum().get<std::uint16_t>() == 0
              && sparseField.declaredType.maximum().get<std::uint16_t>() == 2000
              && sparseField.declaredType.defaultValue().get<std::uint16_t>() == 1000);
static_assert(charCodeSpec.initial == CharCode::Low,
              "enumSpec default must support an underlying char type");
static_assert(!std::is_copy_assignable_v<Field> && std::is_trivially_copyable_v<Field>);
static_assert(sizeof(decltype(limits(1.0f))) == sizeof(float));
}
int main()
{
    expect(testedField.read<float>() == 230.0f, "inferred member read");
    expect(testedField.write(300) == WriteResult::Applied && device.voltage == 300.0f, "native setter receives converted value");
    const auto writes = device.writes;
    expect(testedField.write(0) == WriteResult::InvalidValue && device.writes == writes, "limits block owner call");
    expect(testedField.write(std::numeric_limits<float>::infinity()) == WriteResult::InvalidValue, "nonfinite writes rejected");
    device.voltage = 2000.0f;
    expect(testedField.read<float>() == 2000.0f, "read ignores write limits");
    expect(readOnly.write(1) == WriteResult::ReadOnly, "read only factory");
    expect(enumField.read<std::uint8_t>() == 1, "enum getter produces underlying Scalar");
    expect(enumField.write(2) == WriteResult::Applied && device.modeValue == Mode::Manual, "enum setter receives semantic type");
    expect(enumField.write(3) == WriteResult::InvalidValue, "enum bounds preserved");
    expect(sparseField.write(2000) == WriteResult::Applied
           && device.errorValue == Error::Overcurrent,
           "explicit sparse enum dictionary extends the inferred range");
    expect(sparseField.write(2001) == WriteResult::InvalidValue,
           "explicit sparse enum bounds reject values above the listed range");
    expect(enumField.set(Scalar::fromU16(255)) == WriteResult::Applied,
           "raw scoped-enum callback accepts a representable code; Field::write enforces its dictionary bounds");
    expect(testedField.set(Scalar::fromU16(12)) == WriteResult::Applied,
           "raw typed callback performs checked numeric conversion without descriptor limits");
    expect(global.write(2.0f) == WriteResult::Busy && globalValue == 2.0f, "free setter result preserved");
    expect(global.read<float>() == 2.0f && functionName.read<float>() == 2.0f
           && functionAddress.read<float>() == 2.0f, "free getter: template, name and address forms");
    auto functionPair = telemetry::field("function pair","",getFree,setFree,limits(1.0f,0.0f,5.0f)).materialize();
    expect(functionPair.write(3)==WriteResult::Busy && functionPair.read<float>()==3.0f,
           "parameter function names: getter and setter");
    expect(functionPair.set(Scalar::fromU32(3))==WriteResult::Busy && globalValue == 3.0f,
           "native function setter performs a checked conversion for a manual Scalar call");
    expect(functionPair.set(Scalar::fromF64(1.e300))==WriteResult::InvalidValue && globalValue == 3.0f,
           "native function setter rejects unrepresentable input without calling its owner");
    auto addressPair = telemetry::field("address pair","",&getFree,&setFree).materialize();
    expect(addressPair.write(2.5)==WriteResult::Busy && addressPair.read<float>()==2.5f,
           "parameter function addresses: getter and setter");
    auto inlinePair = telemetry::field("inline pair","",
        []() noexcept {return globalValue;},
        [](float value) noexcept {globalValue=value;return WriteResult::Applied;},
        limits(1.0f,0.0f,4.0f)).materialize();
    expect(inlinePair.write(4)==WriteResult::Applied && globalValue==4.0f,
           "parameter bare lambda pair");
    expect(inlinePair.write(5)==WriteResult::InvalidValue && globalValue==4.0f,
           "parameter lambda pair limits");
    auto plusPair = telemetry::field("plus pair","",
        +[]() noexcept {return globalValue;},
        +[](float value) noexcept {globalValue=value;return WriteResult::Applied;}).materialize();
    expect(plusPair.write(1.5)==WriteResult::Applied && plusPair.read<float>()==1.5f,
           "parameter plus-lambda pair");
    float capturedValue = 2.0f;
    int capturedWrites = 0;
    auto capturedGet = [&capturedValue]() noexcept { return capturedValue; };
    auto capturedSet = [&capturedValue, &capturedWrites](float value) noexcept {
        ++capturedWrites;
        capturedValue = value;
        return WriteResult::Applied;
    };
    const auto capturedField = telemetry::field("captured pair", "V", capturedGet, capturedSet,
                                         limits(2.0f, 1.0f, 4.0f)).materialize();
    const auto capturedCopy = capturedField;
    expect(capturedCopy.read<float>() == 2.0f
           && capturedCopy.write(3.5f) == WriteResult::Applied
           && capturedValue == 3.5f && capturedWrites == 1,
           "borrowed capturing getter and setter");
    expect(capturedCopy.write(5.0f) == WriteResult::InvalidValue && capturedWrites == 1,
           "borrowed capturing field keeps descriptor limits");
    const auto capturedReadOnly = telemetry::field("captured read", "V", capturedGet).materialize();
    expect(capturedReadOnly.read<float>() == 3.5f
           && capturedReadOnly.write(2.0f) == WriteResult::ReadOnly,
           "borrowed capturing read-only getter");
    auto mutableGet = [value = 10.0f]() mutable noexcept {
        value += 1.0f;
        return value;
    };
    const auto mutableField = telemetry::field("mutable getter", "", mutableGet).materialize();
    expect(mutableField.read<float>() == 11.0f && mutableField.read<float>() == 12.0f,
           "borrowed mutable getter keeps closure state");
    const auto constCapturedGet = [&capturedValue]() noexcept { return capturedValue; };
    const auto constCapturedField = telemetry::field("const captured getter", "", constCapturedGet).materialize();
    expect(constCapturedField.read<float>() == 3.5f,
           "borrowed const capturing getter");
    Mode capturedMode = Mode::Off;
    auto capturedEnumGet = [&capturedMode]() noexcept { return capturedMode; };
    auto capturedEnumSet = [&capturedMode](Mode value) noexcept {
        capturedMode = value;
        return WriteResult::Applied;
    };
    const auto capturedEnum = telemetry::field("captured enum", "", capturedEnumGet, capturedEnumSet,
        enumSpec<Mode::Off, Mode::Automatic, Mode::Manual>(Mode::Automatic)).materialize();
    expect(capturedEnum.declaredType.hasEnum()
           && capturedEnum.write(2) == WriteResult::Applied
           && capturedMode == Mode::Manual,
           "borrowed capturing enum preserves semantic type");
    Scalar capturedScalarValue = Scalar::fromU16(12);
    auto capturedScalarGet = [&capturedScalarValue]() noexcept -> Scalar {
        return capturedScalarValue;
    };
    auto capturedScalarSet = [&capturedScalarValue](const Scalar& value) noexcept {
        capturedScalarValue = value;
        return WriteResult::Applied;
    };
    const auto capturedScalar = telemetry::field("captured Scalar", "",
        ScalarType::U16, capturedScalarGet, capturedScalarSet).materialize();
    expect(capturedScalar.read<std::uint16_t>() == 12
           && capturedScalar.write(42) == WriteResult::Applied
           && capturedScalarValue.type() == ScalarType::U16
           && capturedScalarValue.get<std::uint16_t>() == 42,
           "borrowed Scalar callbacks use an explicit declared type");
    expect(clockField.read<std::uint64_t>() == UINT64_MAX, "static getter exact U64");
    expect(lambdaField.read<float>() == globalValue && plusField.read<float>() == globalValue,
           "bare and plus inline lambdas infer type");
    expect(lambdaPair.write(4) == WriteResult::Applied && globalValue == 4.0f, "C++17 named lambda pair");
    expect(lambdaPair.write(6) == WriteResult::InvalidValue, "named lambda pair limits");
    expect(lambdaEnumField.read<std::uint8_t>() == 1 && lambdaEnumField.declaredType.hasEnum(), "named enum lambda");
    expect(explicitType.write(10) == WriteResult::Applied && explicitType.read<float>() == 10.0f, "Scalar explicit escape hatch");
    expect(legacyField.write(0) == WriteResult::Applied && device.legacy == 0, "unnamed enum gap remains numeric");
    expect(legacyField.set(Scalar::fromS32(1000)) == WriteResult::InvalidValue, "unfixed enum out of range not cast");
    const Device constant{};
    const auto constantField = telemetry::field<&Device::get>("const","",constant).materialize();
    expect(constantField.read<float>() == 230.0f, "const lvalue owner");
    Device local;
    auto localField = telemetry::field<&Device::get,&Device::set>("local","",local).materialize();
    const auto copy = localField;
    expect(copy.write(50) == WriteResult::Applied && local.voltage == 50.0f, "copy retains borrowed owner");
    const auto refField = telemetry::field<&Device::refGet>("ref","",local).materialize();
    expect(refField.read<float>() == 50.0f, "const lvalue-qualified method");
    const Field rows[] = {telemetry::field<&Device::get>("v","",local).materialize(),telemetry::field<&Device::mode>("m","",local).materialize()};
    const Catalog catalogs[] = {{"factory",rows}};
    const CatalogIndex index{catalogs};
    expect(index.read<float>(0) == 50.0f && index.read<std::uint8_t>(1) == 1, "same CatalogIndex consumes factory fields");
    std::printf("%d/%d factory checks passed\n", checks-failures,checks);
    return failures != 0;
}
