/*
 * @file Telemetry.hpp
 * @brief Public C++20 telemetry API for native values and structural types.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * The complete native and encoded telemetry API in one include.
 *
 * Applications can include narrower family headers when they need a smaller
 * dependency surface. Endpoint declarations and their structural metadata are
 * shared by native calls and encoded dispatch; transport state stays outside
 * this library.
 */

#ifndef TELEMETRY_TELEMETRY_HPP
#define TELEMETRY_TELEMETRY_HPP
#pragma once

#include "codec/Codec.hpp"
#include "model/Adapter.hpp"
#include "model/Model.hpp"
#include "reflection/Reflection.hpp"
#include "result/ServiceResult.hpp"
#include "result/BorrowedServiceResult.hpp"
#include "result/BorrowedValue.hpp"
#include "result/NativeCallResult.hpp"
#include "result/FieldReadResult.hpp"
#include "type/Registry.hpp"

#endif
