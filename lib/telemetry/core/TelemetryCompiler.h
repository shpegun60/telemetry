/**
 * @file TelemetryCompiler.h
 * @brief Compiler attributes shared by the small telemetry adapters.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_COMPILER_H
#define TELEMETRY_COMPILER_H

// Keep small adapters at the call site so known IDs, types and bindings fold
// even in size-optimized builds. Unsupported compilers use ordinary inline.
#if defined(_MSC_VER)
#define TELEMETRY_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define TELEMETRY_FORCE_INLINE inline __attribute__((always_inline))
#else
#define TELEMETRY_FORCE_INLINE inline
#endif

#endif
