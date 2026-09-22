/**
 * @file BinaryFormat.hpp
 * @brief Stable telemetry resource wire codes, independent of C++ object layout.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#pragma once
#include <telemetry/core/TelemetryScalar.h>
#include <cstdint>

namespace telemetry_resource
{
// v2 widens all fingerprints to u64. v1 headers must not be decoded as v2.
inline constexpr std::uint16_t binaryMajor = 2;
inline constexpr std::uint16_t binaryMinor = 0;
inline constexpr std::uint32_t metadataHeaderSize = 44;
inline constexpr std::uint32_t valuesHeaderSize = 20;
inline constexpr std::uint32_t recordHeaderSize = 8;

enum class WireScalarType : std::uint8_t
{
    Null = 0,
    Bool = 1,
    U8 = 2,
    U16 = 3,
    U32 = 4,
    U64 = 5,
    S8 = 6,
    S16 = 7,
    S32 = 8,
    S64 = 9,
    F32 = 10,
    F64 = 11
};
enum class ScalarState : std::uint8_t
{
    Null = 0,
    Value = 1
};
enum class ValueStatus : std::uint8_t
{
    Available = 0,
    Unavailable = 1
};
enum class SchemaRecord : std::uint8_t
{
    FieldFlagDefinition = 1,
    Catalog = 2,
    Field = 3,
    FieldEnumEntry = 4
};
enum class CommandRecord : std::uint8_t
{
    Catalog = 1,
    Command = 2,
    Parameter = 3,
    ParameterEnum = 4
};

enum AccessFlag : std::uint8_t
{
    Readable = 1,
    Writable = 2
};

enum FieldRecordFlag : std::uint8_t
{
    Reserved = 1
};

enum ParameterPresence : std::uint8_t
{
    HasName = 1,
    HasUnit = 2
};

constexpr WireScalarType toWireType(telemetry::ScalarType type) noexcept
{
    using T = telemetry::ScalarType;
    switch (type)
    {
        case T::Bool:
            return WireScalarType::Bool;
        case T::U8:
            return WireScalarType::U8;
        case T::U16:
            return WireScalarType::U16;
        case T::U32:
            return WireScalarType::U32;
        case T::U64:
            return WireScalarType::U64;
        case T::S8:
            return WireScalarType::S8;
        case T::S16:
            return WireScalarType::S16;
        case T::S32:
            return WireScalarType::S32;
        case T::S64:
            return WireScalarType::S64;
        case T::F32:
            return WireScalarType::F32;
        case T::F64:
            return WireScalarType::F64;
        case T::Null:
            return WireScalarType::Null;
    }
    return WireScalarType::Null;
}

constexpr std::uint8_t payloadSize(WireScalarType type) noexcept
{
    switch (type)
    {
        case WireScalarType::Bool:
        case WireScalarType::U8:
        case WireScalarType::S8:
            return 1;
        case WireScalarType::U16:
        case WireScalarType::S16:
            return 2;
        case WireScalarType::U32:
        case WireScalarType::S32:
        case WireScalarType::F32:
            return 4;
        case WireScalarType::U64:
        case WireScalarType::S64:
        case WireScalarType::F64:
            return 8;
        case WireScalarType::Null:
            return 0;
    }
    return 0;
}
} // namespace telemetry_resource
