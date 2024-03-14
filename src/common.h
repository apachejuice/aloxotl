// apachejuice, 25.02.2024
// See LICENSE for details.
#ifndef __ALOXOTL_COMMON__
#define __ALOXOTL_COMMON__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION
#define DEBUG_STRESS_GC
// #define DEBUG_LOG_GC

#define UINT8_COUNT (UINT8_MAX + 1)

// Friendlier names.

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef size_t size;

#endif
