/*
 * @file Fixture.hpp
 * @brief Multi-TU qualification using the existing mixed/4 KiB fixture.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */

#ifndef TELEMETRY_TESTS_STRUCTURED_QUALIFICATION_FIXTURE_HPP
#define TELEMETRY_TESTS_STRUCTURED_QUALIFICATION_FIXTURE_HPP
#pragma once

#include "../traversal/Fixture.hpp"
#include <resource/telemetry/v3/Descriptor.hpp>
#include <resource/telemetry/v3/ValuesFile.hpp>
#ifndef QUALIFICATION_ARM
#include <cstdio>
#include <source_location>
#endif

namespace qualification {
namespace ts = telemetry;
namespace rs = resource::telemetry::v3;

// Counters are shared across translation units. A test counts a checked
// condition, rather than treating a successful compiler command as a check.
inline unsigned checks = 0;
inline unsigned failures = 0;
inline void check(bool condition
#ifndef QUALIFICATION_ARM
                  ,
                  std::source_location where = std::source_location::current()
#endif
                      ) noexcept
{
	++checks;
	if (!condition) {
		++failures;
#ifndef QUALIFICATION_ARM
		std::fprintf(stderr, "%s:%u: check %u failed\n", where.file_name(), where.line(), checks);
#endif
	}
}

// The provider owns immutable metadata; consumers borrow runtime views.
ts::ModelView modelView() noexcept;
std::span<const std::byte> descriptorBytes() noexcept;
std::uint64_t fingerprint() noexcept;
extern ts::Workspace sharedWorkspace;
resource::ReadResult readValues(resource::Cursor, std::span<std::byte>) noexcept;

extern "C" void consumer_typed() noexcept;
extern "C" void consumer_encoded() noexcept;
} // namespace qualification

#endif // TELEMETRY_TESTS_STRUCTURED_QUALIFICATION_FIXTURE_HPP
