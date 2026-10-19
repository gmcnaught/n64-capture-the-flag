/* Q16.16 fixed point.
 *
 * Shared by host (clang) and N64 (mips64-elf-gcc). Right shift of negative
 * values is implementation-defined in C; both compilers implement it as an
 * arithmetic shift, and this header is the only place that relies on it.
 */
#ifndef CORE_FIXED_H
#define CORE_FIXED_H

#include <stdint.h>

typedef int32_t fx;

#define FX_ONE   ((fx)0x00010000)
#define FX_HALF  ((fx)0x00008000)
#define FX(px)   ((fx)(px) * FX_ONE)

/* Whole pixels, rounded toward negative infinity. */
static inline int16_t fx_floor(fx v) { return (int16_t)(v >> 16); }

/* Whole pixels, rounded to nearest (ties toward positive infinity). */
static inline int16_t fx_round(fx v) { return (int16_t)((v + FX_HALF) >> 16); }

#endif
