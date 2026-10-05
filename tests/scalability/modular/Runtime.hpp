/*
 * Small public runtime boundary for modular telemetry consumers.
 *
 * This header exposes neither module DTOs nor table/binding aliases. Returned
 * views borrow process-lifetime composition storage; callers serialize owner
 * access and supply their own operation buffers and Workspace.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#ifndef TELEMETRY_TESTS_SCALABILITY_MODULAR_RUNTIME_HPP
#define TELEMETRY_TESTS_SCALABILITY_MODULAR_RUNTIME_HPP
#pragma once

#include <cstdint>

namespace telemetry {
struct ModelView;
}

namespace resource {
class FileSystemView;
}

namespace modular {
const telemetry::ModelView& modelView() noexcept;
resource::FileSystemView files() noexcept;
// Metadata is cached automatically on its first required fingerprint query.
std::uint64_t fingerprint() noexcept;
std::uint32_t typeCount() noexcept;
} // namespace modular
#endif
