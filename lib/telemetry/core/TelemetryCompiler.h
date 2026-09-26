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
// These attributes affect code generation only; correctness never depends on
// successful inlining. NOINLINE is also used to keep measurement probes honest.
#if defined(_MSC_VER)
#define TELEMETRY_FORCE_INLINE __forceinline
#define TELEMETRY_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define TELEMETRY_FORCE_INLINE inline __attribute__((always_inline))
#define TELEMETRY_NOINLINE __attribute__((noinline))
#else
#define TELEMETRY_FORCE_INLINE inline
#define TELEMETRY_NOINLINE
#endif

// GCC's -Os can save a callee-saved register before a tiny dispatch branch
// solely for the uncommon conversion call. Use speed optimization for that
// thunk only. The attribute is identical across translation units; it never
// depends on __OPTIMIZE_SIZE__, and does not enable fast-math.
#if defined(__GNUC__) && !defined(__clang__)
#define TELEMETRY_OPTIMIZE_SPEED __attribute__((optimize("O2")))
#else
#define TELEMETRY_OPTIMIZE_SPEED
#endif

#endif
