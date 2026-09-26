/*
 * @file TypesProbe.cpp
 * @brief Stage 03 fixed wire type classification and size checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry_structured/type/Traits.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

struct Empty {};
struct Reading { float value; std::uint16_t status; };
struct Samples { std::array<Reading, 3> samples; bool enabled; };
struct Defaulted { std::uint8_t value = 42; };
struct Repeated { Reading first; Reading second; };
enum class Mode : std::uint8_t { Off, On };
enum class DefaultMode { Off, On };

using telemetry::structured::ScalarCode;
using telemetry::structured::TypeKind;

static_assert(telemetry::structured::wireSize<void> == 0);
static_assert(telemetry::structured::wireSize<bool> == 1);
static_assert(telemetry::structured::wireSize<std::uint8_t> == 1);
static_assert(telemetry::structured::wireSize<std::int8_t> == 1);
static_assert(telemetry::structured::wireSize<std::uint16_t> == 2);
static_assert(telemetry::structured::wireSize<std::int16_t> == 2);
static_assert(telemetry::structured::wireSize<std::uint32_t> == 4);
static_assert(telemetry::structured::wireSize<std::int32_t> == 4);
static_assert(telemetry::structured::wireSize<std::uint64_t> == 8);
static_assert(telemetry::structured::wireSize<std::int64_t> == 8);
static_assert(telemetry::structured::wireSize<float> == 4);
static_assert(telemetry::structured::wireSize<double> == 8);
static_assert(telemetry::structured::wireSize<Mode> == 1);
static_assert(telemetry::structured::wireSize<DefaultMode> == sizeof(int));
static_assert(telemetry::structured::wireSize<Empty> == 0);
static_assert(telemetry::structured::wireSize<Reading> == 6);
static_assert(telemetry::structured::wireSize<Samples> == 19);
static_assert(telemetry::structured::wireSize<Defaulted> == 1);
static_assert(telemetry::structured::wireSize<Repeated> == 12);
static_assert(telemetry::structured::wireSize<std::array<Empty, 0>> == 0);
static_assert(telemetry::structured::wireSize<std::array<Empty, 3>> == 0);
static_assert(telemetry::structured::wireSize<
                  std::array<std::array<std::uint64_t, 65536>, 2>> == 1048576);
static_assert(telemetry::structured::expandedNodes<Samples> == 12);
static_assert(telemetry::structured::Type<Samples>::depth == 3);
static_assert(sizeof(Reading) > telemetry::structured::wireSize<Reading>);
static_assert(telemetry::structured::typeKind<void> == TypeKind::Void);
static_assert(telemetry::structured::typeKind<bool> == TypeKind::Scalar);
static_assert(telemetry::structured::typeKind<Mode> == TypeKind::Enum);
static_assert(telemetry::structured::typeKind<Reading> == TypeKind::Struct);
static_assert(telemetry::structured::typeKind<std::array<int, 1>> == TypeKind::Array);
static_assert(telemetry::structured::Type<bool>::code == ScalarCode::Bool);
static_assert(telemetry::structured::Type<std::uint8_t>::code == ScalarCode::U8);
static_assert(telemetry::structured::Type<std::int8_t>::code == ScalarCode::S8);
static_assert(telemetry::structured::Type<std::uint16_t>::code == ScalarCode::U16);
static_assert(telemetry::structured::Type<std::int16_t>::code == ScalarCode::S16);
static_assert(telemetry::structured::Type<std::uint32_t>::code == ScalarCode::U32);
static_assert(telemetry::structured::Type<std::int32_t>::code == ScalarCode::S32);
static_assert(telemetry::structured::Type<std::uint64_t>::code == ScalarCode::U64);
static_assert(telemetry::structured::Type<std::int64_t>::code == ScalarCode::S64);
static_assert(telemetry::structured::Type<float>::code == ScalarCode::F32);
static_assert(telemetry::structured::Type<double>::code == ScalarCode::F64);
static_assert(std::numeric_limits<std::int8_t>::lowest() == -128);
static_assert(std::numeric_limits<std::int16_t>::lowest() == -32768);
static_assert(std::numeric_limits<std::int32_t>::lowest() == -2147483647 - 1);
static_assert(std::numeric_limits<std::int64_t>::lowest() ==
              -9223372036854775807LL - 1);

int main() { return telemetry::structured::wireSize<Samples> == 19 ? 0 : 1; }
