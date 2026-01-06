#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern int LX_ARGC;
extern const char** LX_ARGV;

#ifdef DEBUG
#define DEBUG_TRACE_EXECUTION

#define DEBUG_STRESS_GC
#define DEBUG_LOG_GC
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

// Branch prediction hints for performance-critical paths
#if defined(__GNUC__) || defined(__clang__)
  #define LIKELY(x)   __builtin_expect(!!(x), 1)
  #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
  #define LIKELY(x)   (x)
  #define UNLIKELY(x) (x)
#endif

#endif
