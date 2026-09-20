#ifndef DEMO_CATALOG_H
#define DEMO_CATALOG_H

#include "Telemetry.h"
#include <cmath>
#include <limits>

namespace demo {

class Sensor {
public:
    double temperature() const noexcept;
    bool enabled() const noexcept;
    void advance() noexcept;

private:
    double temperature_ = 24.0;
    bool enabled_ = true;
};

using telemetry::field;
using telemetry::WriteResult;
using telemetry::CommandResult;

enum class Mode : std::uint16_t { Off, Auto, Manual };

// Numeric values follow the row order below; these names do not add a lookup.
enum class MeterField : std::size_t { Voltage, Current, Power, Counter, VoltageLimit, Mode, Count };
enum class MeterCommand : std::size_t { Reset, Configure, Count };

struct Meter {
    float voltage = 230.0f;
    float current = 2.0f;
    std::uint32_t counter = 0;
    float threshold = 250.0f;
    Mode mode = Mode::Auto;

    float readVoltage() const noexcept { return voltage; }
    float readCurrent() const noexcept { return current; }
    float readPower() const noexcept { return voltage * current / 1000.0f; }
    std::uint32_t readCounter() const noexcept { return counter; }
    float readThreshold() const noexcept { return threshold; }
    Mode readMode() const noexcept { return mode; }
    WriteResult setMode(Mode next) noexcept { mode=next; return WriteResult::Applied; }
    WriteResult setThreshold(float next) noexcept
    {
        if (!std::isfinite(next) || next <= 0.0f || next > 1000.0f) return WriteResult::InvalidValue;
        threshold = next;
        return WriteResult::Applied;
    }
    CommandResult reset() noexcept { counter=0; return CommandResult::Executed; }
    CommandResult configure(float next, Mode nextMode) noexcept
    { threshold=next; mode=nextMode; return CommandResult::Executed; }
};

inline Meter meter;
inline Sensor sensor;

// The function signatures define types. Only labels and optional limits remain.
inline constexpr telemetry::FieldTable meterFields{
    field<&Meter::readVoltage>("Ua", "V", meter),
    field<&Meter::readCurrent>("Ia", "A", meter),
    field<&Meter::readPower>("P", "kW", meter),
    field<&Meter::readCounter>("WinCnt", "", meter),
    field<&Meter::readThreshold, &Meter::setThreshold>("VoltageLimit", "V",
        meter, telemetry::limits(250.0f, 1.0f, 1000.0f))
        .withFlags(telemetry::FieldFlag::Persistent),
    field<&Meter::readMode, &Meter::setMode>("Mode", "", meter,
        telemetry::limits(Mode::Auto))
        .withFlags(telemetry::FieldFlag::Persistent),
};

static_assert(telemetry::names_unique(meterFields.data(), meterFields.size()));
static_assert(meterFields.size() == static_cast<std::size_t>(MeterField::Count));

// Free function templates retain exact integer values without Scalar callbacks.
template <class T> T maximumValue() noexcept { return std::numeric_limits<T>::max(); }
template <class T> T minimumValue() noexcept { return std::numeric_limits<T>::lowest(); }
inline constexpr telemetry::FieldTable integerFields{
    field<&maximumValue<std::uint8_t>>("U8", ""),
    field<&maximumValue<std::uint16_t>>("U16", ""),
    field<&maximumValue<std::uint32_t>>("U32", ""),
    field<&maximumValue<std::uint64_t>>("U64", ""),
    field<&minimumValue<std::int8_t>>("S8", ""),
    field<&minimumValue<std::int16_t>>("S16", ""),
    field<&minimumValue<std::int32_t>>("S32", ""),
    field<&minimumValue<std::int64_t>>("S64", ""),
};
static_assert(telemetry::names_unique(integerFields.data(), integerFields.size()));

// The table owns inline metadata. Runtime lookup still sees ordinary Commands.
inline constexpr telemetry::CommandTable meterCommands{
    telemetry::command<&Meter::reset>("Reset counter", meter),
    telemetry::command<&Meter::configure>("Configure meter", meter,
        telemetry::arg<0>("Voltage limit", "V", 250.0f, 1.0f, 1000.0f),
        telemetry::arg<1>("Mode", "", Mode::Auto))};
static_assert(telemetry::commandNamesUnique(meterCommands.data(), meterCommands.size()));
static_assert(meterCommands.size() == static_cast<std::size_t>(MeterCommand::Count));
inline constexpr telemetry::CommandCatalogTable commands{telemetry::group("meter",meterCommands)};
inline constexpr auto commandIndex = commands.index();

// The same declaration form also binds methods on another source object.
inline constexpr telemetry::FieldTable sensorFields{
    field<&Sensor::temperature>("Temperature", "degC", sensor),
    field<&Sensor::enabled>("Enabled", "", sensor),
};
inline constexpr telemetry::FieldCatalogTable fields{
    telemetry::group("meter", meterFields),
    telemetry::group("sensor", sensorFields),
    telemetry::group("integers", integerFields),
};
inline constexpr auto fieldIndex = fields.index();

void advance() noexcept;

} // namespace demo
#endif
