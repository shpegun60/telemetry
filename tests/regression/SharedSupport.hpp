// Maintained shared contract checks (MIT).

// Shares only test includes and the historical regression namespace alias.
// Keeping this support header small lets each case continue to expose the production API it checks.

#ifndef TELEMETRY_TESTS_REGRESSION_SHAREDSUPPORT_HPP
#define TELEMETRY_TESTS_REGRESSION_SHAREDSUPPORT_HPP
#pragma once

#include <cstdio>
#include <cstdlib>
inline unsigned sharedChecks = 0;
#define CHECK(...)                                                                                 \
	do {                                                                                           \
		++sharedChecks;                                                                            \
		if (!(__VA_ARGS__)) {                                                                      \
			std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__);                         \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

inline void reportChecks()
{
	std::printf("CHECKS %u\n", sharedChecks);
}

#endif // TELEMETRY_TESTS_REGRESSION_SHAREDSUPPORT_HPP
