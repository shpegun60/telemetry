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

// Public construction starts with field()/command() and owning local tables.
// Catalog tables borrow those locals; indexes are transport-facing views.
// Keep the ABI guard in every consumer even when neither JSON source is linked.
#include "core/TelemetryId.h"
#include "slot/TelemetryOwnerSlot.h"
#include "slot/TelemetryFunctionSlot.h"
#include "slot/TelemetryContextFunctionSlot.h"
#include "slot/TelemetryDelegateRefSlot.h"
#include "slot/TelemetryDelegateSlot.h"
#include "field/TelemetryEnum.h"
#include "field/TelemetryFieldFactory.h"
#include "field/TelemetryFieldTable.h"
#include "catalog/TelemetryFieldCatalogTable.h"
#include "catalog/TelemetryIndex.h"
#include "command/TelemetryCommandFactory.h"
#include "command/TelemetryCommandCatalogIndex.h"
#include "command/TelemetryCommandIndex.h"
#include "command/TelemetryCommandTable.h"
#include "command/TelemetryCommandCatalogTable.h"
#include "abi/TelemetryAbi.h"

#endif
