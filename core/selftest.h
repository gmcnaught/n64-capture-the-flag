#ifndef CORE_SELFTEST_H
#define CORE_SELFTEST_H

#include <stdint.h>

/* Runs a fixed, input-free movement simulation and returns a hash of every
 * frame's integer position. All platforms must print the same value; a
 * mismatch means a compiler/target difference in integer semantics. */
uint32_t core_selftest(void);

/* Recorded 2026-10-17 on host (arm64 clang); N64 (ares, MiSTer) matches.
 * Change only together with an intentional change to core_selftest(). */
#define CORE_SELFTEST_EXPECTED 0xd87cfc17u

#endif
