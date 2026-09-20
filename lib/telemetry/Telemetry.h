/**
 * @file Telemetry.h
 * @brief Core telemetry fields, catalogs, commands, direct indexes and ABI guard.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE in this directory.
 *
 * JSON is optional: include serialization/TelemetryJson.h separately and link
 * serialization/TelemetryJson.cpp only when serialization is required.
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "core/TelemetryId.h"
#include "field/TelemetryEnum.h"
#include "field/TelemetryFieldFactory.h"
#include "catalog/TelemetryIndex.h"
#include "command/TelemetryCommandFactory.h"
#include "command/TelemetryCommandCatalogIndex.h"
#include "command/TelemetryCommandIndex.h"
#include "command/TelemetryCommandTable.h"
#include "abi/TelemetryAbi.h"

#endif
