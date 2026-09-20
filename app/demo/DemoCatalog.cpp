#include "DemoCatalog.h"
#include <iterator>
#include <cmath>
#include <limits>

namespace demo {
namespace {

using telemetry::Field;
using telemetry::WriteResult;
using telemetry::makeId;
using telemetry::makeField;
using telemetry::CommandResult;

enum class Mode : std::uint16_t { Off, Auto, Manual };

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

Meter meter;

// The function signatures define types. Only labels and optional limits remain.
constexpr Field meterFields[] = {
    makeField<&Meter::readVoltage>(makeId(0, 0), "Ua", "V", meter),
    makeField<&Meter::readCurrent>(makeId(0, 1), "Ia", "A", meter),
    makeField<&Meter::readPower>(makeId(0, 2), "P", "kW", meter),
    makeField<&Meter::readCounter>(makeId(0, 3), "WinCnt", "", meter),
    makeField<&Meter::readThreshold, &Meter::setThreshold>(makeId(0, 4), "VoltageLimit", "V",
        meter, telemetry::limits(250.0f, 1.0f, 1000.0f)),
    makeField<&Meter::readMode, &Meter::setMode>(makeId(0, 5), "Mode", "", meter,
        telemetry::enumSpec<Mode::Off, Mode::Auto, Mode::Manual>(Mode::Auto)),
};

static_assert(telemetry::names_unique(meterFields, std::size(meterFields)));
constexpr telemetry::Catalog meterCatalog{0, "meter", meterFields};

// Free function templates retain exact integer values without Scalar callbacks.
template <class T> T maximumValue() noexcept { return std::numeric_limits<T>::max(); }
template <class T> T minimumValue() noexcept { return std::numeric_limits<T>::lowest(); }
constexpr Field integerFields[] = {
    makeField<&maximumValue<std::uint8_t>>(makeId(2, 0), "U8", ""),
    makeField<&maximumValue<std::uint16_t>>(makeId(2, 1), "U16", ""),
    makeField<&maximumValue<std::uint32_t>>(makeId(2, 2), "U32", ""),
    makeField<&maximumValue<std::uint64_t>>(makeId(2, 3), "U64", ""),
    makeField<&minimumValue<std::int8_t>>(makeId(2, 4), "S8", ""),
    makeField<&minimumValue<std::int16_t>>(makeId(2, 5), "S16", ""),
    makeField<&minimumValue<std::int32_t>>(makeId(2, 6), "S32", ""),
    makeField<&minimumValue<std::int64_t>>(makeId(2, 7), "S64", ""),
};
static_assert(telemetry::names_unique(integerFields, std::size(integerFields)));
constexpr telemetry::Catalog integerCatalog{2, "integers", integerFields};

// Metadata owns only labels/defaults/bounds. Types come from configure's signature.
constexpr auto configureArgs = telemetry::commandArgs(
    telemetry::arg("Voltage limit", "V", 250.0f, 1.0f, 1000.0f),
    telemetry::arg("Mode", "",
        telemetry::enumSpec<Mode::Off, Mode::Auto, Mode::Manual>(Mode::Auto)));
constexpr telemetry::Command meterCommands[] = {
    telemetry::makeCommand<&Meter::reset>(makeId(0, 0), "Reset counter", meter),
    telemetry::makeCommand<&Meter::configure>(makeId(0, 1), "Configure meter", meter, configureArgs),
};
static_assert(telemetry::commandNamesUnique(meterCommands, std::size(meterCommands)));
constexpr telemetry::CommandCatalog commandCatalogs[] = {
    {0, "meter", meterCommands},
};
static_assert(telemetry::commandCatalogNamesUnique(commandCatalogs, std::size(commandCatalogs)));
constexpr telemetry::CommandCatalogIndex commandIndex{commandCatalogs};

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
          makeField<&Sensor::temperature>(makeId(1, 0), "Temperature", "degC", sensor_),
          makeField<&Sensor::enabled>(makeId(1, 1), "Enabled", "", sensor_),
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

const telemetry::CommandCatalogIndex& DemoCatalog::commands() const noexcept { return commandIndex; }

} // namespace demo
