/* Application boundary: no telemetry templates leave the implementation. MIT. */

#ifndef TELEMETRY_TESTS_STRUCTURED_RESOURCES_DEVICERESOURCES_HPP
#define TELEMETRY_TESTS_STRUCTURED_RESOURCES_DEVICERESOURCES_HPP
#pragma once

#include <resource/FileSystem.hpp>
resource::FileSystemView deviceResources() noexcept;
void processDeviceResources(std::uint32_t nowMs) noexcept;
#ifdef WITH_RPC_CLIENT
bool exerciseDeviceRpcClient() noexcept;
#endif
#ifdef WITH_RPC_SESSIONS
bool exerciseDeviceRpcSessions() noexcept;
#endif

#endif // TELEMETRY_TESTS_STRUCTURED_RESOURCES_DEVICERESOURCES_HPP
