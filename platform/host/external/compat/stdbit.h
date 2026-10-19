/* Minimal C23 <stdbit.h> for toolchains without one (Apple's SDK): only
 * what external/libxm uses. */
#ifndef COMPAT_STDBIT_H
#define COMPAT_STDBIT_H

#define stdc_count_ones(x) ((unsigned)__builtin_popcountll((unsigned long long)(x)))

#endif
