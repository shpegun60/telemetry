#include "DemoCatalog.h"
#include "TelemetryEnum.h"

#include <iterator>
#include <cmath>

namespace demo {
namespace {

using telemetry::Field;
using telemetry::Getter;
using telemetry::Scalar;
using telemetry::ScalarType;
using telemetry::WriteResult;
using telemetry::makeId;

enum class Mode : std::uint16_t { Off, Auto, Manual };

struct Meter {
    float voltage = 230.0f;
    float current = 2.0f;
    std::uint32_t counter = 0;
    float threshold = 250.0f;
    Mode mode = Mode::Auto;

    float readVoltage() const noexcept { return voltage; }
    bool setThreshold(float next) noexcept
    {
        if (!std::isfinite(next) || next <= 0.0f || next > 1000.0f) return false;
        threshold = next;
        return true;
    }
};

Meter meter;

// The source returns an ordinary float and knows nothing about Scalar.
// Both readUa and &readUa can be written directly into a table row.
float readUa() noexcept
{
    return meter.voltage;
}

// All five Ua rows read the same changing value, making the binding forms
// easy to compare in the window. Every row here is constant-initialized.
constexpr Field meterFields[] = {
    // 1. Capture-free lambdas: both bare [] and explicit +[] work in rows.
    {makeId(0, 0), "Ua", "V", ScalarType::F32, []() noexcept { return meter.voltage; }},
    {makeId(0, 1), "Ia", "A", ScalarType::F32, +[]() noexcept { return meter.current; }},
    {makeId(0, 2), "P", "kW", ScalarType::F32, +[]() noexcept { return meter.voltage * meter.current / 1000.0f; }},
    {makeId(0, 3), "WinCnt", "", ScalarType::U32, +[]() noexcept { return meter.counter; }},

    // 2. Ordinary function name (implicitly converted to a function pointer).
    {makeId(0, 4), "UaFunction", "V", ScalarType::F32, readUa},
    // 3. Explicit address of that same function.
    {makeId(0, 5), "UaAddress", "V", ScalarType::F32, &readUa},
    // 4. The function supplied as a template argument.
    {makeId(0, 6), "UaBind", "V", ScalarType::F32, Getter::bind<&readUa>()},
    // 5. A const method on a known global object, also constexpr-bindable.
    {makeId(0, 7), "UaMethod", "V", ScalarType::F32, Getter::bind<&Meter::readVoltage>(meter)},
    {makeId(0, 8), "VoltageLimit", "V", telemetry::numericType<float>(1.0f, 1000.0f, 250.0f),
     []() noexcept { return meter.threshold; },
     [](const Scalar& value) noexcept {
         if (value.type() != ScalarType::F32) return WriteResult::InvalidValue;
         return meter.setThreshold(value.get<float>()) ? WriteResult::Applied : WriteResult::InvalidValue;
     }},
    // Enum names belong only to the schema; source callbacks use numbers.
    {makeId(0, 9), "Mode", "", telemetry::enumType<Mode>(Mode::Auto),
     []() noexcept { return static_cast<std::underlying_type_t<Mode>>(meter.mode); },
     [](const Scalar& value) noexcept {
         meter.mode = static_cast<Mode>(value.get<std::uint16_t>());
         return WriteResult::Applied;
     }},
};

static_assert(telemetry::names_unique(meterFields, std::size(meterFields)));
constexpr telemetry::Catalog meterCatalog{0, "meter", meterFields};

// Fixed-width integer examples also exercise exact 64-bit display/JSON.
constexpr Field integerFields[] = {
    {makeId(2, 0), "U8", "", ScalarType::U8, []() noexcept { return Scalar::fromU8(UINT8_MAX); }},
    {makeId(2, 1), "U16", "", ScalarType::U16, []() noexcept { return Scalar::fromU16(UINT16_MAX); }},
    {makeId(2, 2), "U32", "", ScalarType::U32, []() noexcept { return Scalar::fromU32(UINT32_MAX); }},
    {makeId(2, 3), "U64", "", ScalarType::U64, []() noexcept { return Scalar::fromU64(UINT64_MAX); }},
    {makeId(2, 4), "S8", "", ScalarType::S8, []() noexcept { return Scalar::fromS8(INT8_MIN); }},
    {makeId(2, 5), "S16", "", ScalarType::S16, []() noexcept { return Scalar::fromS16(INT16_MIN); }},
    {makeId(2, 6), "S32", "", ScalarType::S32, []() noexcept { return Scalar::fromS32(INT32_MIN); }},
    {makeId(2, 7), "S64", "", ScalarType::S64, []() noexcept { return Scalar::fromS64(INT64_MIN); }},
};
static_assert(telemetry::names_unique(integerFields, std::size(integerFields)));
constexpr telemetry::Catalog integerCatalog{2, "integers", integerFields};

} // namespace

double Sensor::temperature() const noexcept
{
    return temperature_;
}

bool Sensor::enabled() const noexcept
{
    return enabled_;
}

void Sensor::advance() noexcept
{
    temperature_ = temperature_ >= 26.0 ? 24.0 : temperature_ + 0.25;
    enabled_ = !enabled_;
}

DemoCatalog::DemoCatalog() noexcept
    : sensorFields_{
          // Runtime binding: each DemoCatalog owns its own sensor instance.
          {makeId(1, 0), "Temperature", "degC", ScalarType::F64, Getter::bind<&Sensor::temperature>(sensor_)},
          {makeId(1, 1), "Enabled", "", ScalarType::Bool, Getter::bind<&Sensor::enabled>(sensor_)},
      }
    , catalogs_{
          meterCatalog,
          {1, "sensor", sensorFields_},
          integerCatalog,
      }
    , index_(catalogs_)
{
}

void DemoCatalog::advance() noexcept
{
    ++meter.counter;
    meter.voltage = 230.0f + static_cast<float>(meter.counter % 5) * 0.5f;
    meter.current = 2.0f + static_cast<float>(meter.counter % 3) * 0.1f;
    sensor_.advance();
}

} // namespace demo
