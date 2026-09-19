/**
 * @file TelemetryCacheline.h
 * @brief Compile-time cache-line alignment defaults and explicit target override.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_CACHELINE_H
#define TELEMETRY_CACHELINE_H

#include <cstddef>

// Target defaults are alignment policies, not a runtime hardware query.
// Override for the actual target if its cache geometry differs. All translation
// units and libraries must use the same value: it changes the in-memory ABI.
#if defined(TELEMETRY_FORCE_CACHELINE) && defined(TELEMETRY_CACHELINE_BYTES)
#if TELEMETRY_FORCE_CACHELINE != TELEMETRY_CACHELINE_BYTES
#error "Conflicting telemetry cache-line overrides"
#endif
#endif

#ifndef TELEMETRY_CACHELINE_BYTES
#if defined(TELEMETRY_FORCE_CACHELINE)
#define TELEMETRY_CACHELINE_BYTES TELEMETRY_FORCE_CACHELINE
#elif (defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M') \
    || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) \
    || defined(__ARM_ARCH_8M_BASE__) || defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8_1M_MAIN__)
#define TELEMETRY_CACHELINE_BYTES 32
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define TELEMETRY_CACHELINE_BYTES 128
#elif defined(__AVR__) || defined(__MSP430__) || defined(__XTENSA__) || defined(ESP_PLATFORM)
#define TELEMETRY_CACHELINE_BYTES 32
#elif defined(__riscv) && !defined(__linux__) && !defined(_WIN32)
#define TELEMETRY_CACHELINE_BYTES 32
#else
#define TELEMETRY_CACHELINE_BYTES 64
#endif
#endif

#if TELEMETRY_CACHELINE_BYTES < 32 || ((TELEMETRY_CACHELINE_BYTES & (TELEMETRY_CACHELINE_BYTES - 1)) != 0)
#error "TELEMETRY_CACHELINE_BYTES must be a power of two, at least 32"
#endif

namespace telemetry {
inline constexpr std::size_t cacheLineBytes = TELEMETRY_CACHELINE_BYTES;
}

#endif
