/*
 * @file UnsupportedNameCompileFail.cpp
 * @brief Reject a reflected member name outside the v1 ASCII identifier set.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <boost/pfr/core_name.hpp>

// This must fail for the intended contract, after the shared type parsed.
static_assert(telemetry_structured_probe::asciiIdentifier(
                  boost::pfr::get_name<0, telemetry_structured_probe::NonAsciiName>()),
              "Automatic reflected member names must be ASCII");
