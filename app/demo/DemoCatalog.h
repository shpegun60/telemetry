#ifndef DEMO_CATALOG_H
#define DEMO_CATALOG_H

#include "Telemetry.h"

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

// Owns the sensor and its field array. Moving/copying this object would
// leave its getters/catalogs pointing to the original sensor/array.
class DemoCatalog {
public:
    DemoCatalog() noexcept;
    DemoCatalog(const DemoCatalog&) = delete;
    DemoCatalog& operator=(const DemoCatalog&) = delete;
    DemoCatalog(DemoCatalog&&) = delete;
    DemoCatalog& operator=(DemoCatalog&&) = delete;

    const telemetry::Catalog* catalogs() const noexcept { return index_.data(); }
    std::size_t count() const noexcept { return index_.size(); }
    const telemetry::CatalogIndex& index() const noexcept { return index_; }
    const telemetry::Field* find(telemetry::FieldId id) const noexcept { return index_.find(id); }
    const telemetry::CommandCatalogIndex& commands() const noexcept;
    void advance() noexcept;

private:
    Sensor sensor_;
    const telemetry::Field sensorFields_[2];
    const telemetry::Catalog catalogs_[3];
    const telemetry::CatalogIndex index_;
};

} // namespace demo

#endif
