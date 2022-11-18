#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern int LX_ARGC;
extern const char** LX_ARGV;

#define NAN_BOXING

#ifdef DEBUG
#define DEBUG_TRACE_EXECUTION
#define DEBUG_PRINT_CODE

#define DEBUG_STRESS_GC
#define DEBUG_LOG_GC1
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
