/* CPU players for the platforms (core/bot.h): the navigation graph of the
 * loaded level and a bot per player slot.
 *
 * The graph comes from the level's .nav file (tools/navbuild.c, built with
 * the assets); if there is none or it was built from other cells or
 * tuning, cpu_set_level starts building it and cpu_build finishes it a
 * few nodes at a time (the host takes a second or a few; the N64 would
 * take minutes and does not). CPU players stand still until it is ready.
 */
#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#include "bot.h"
#include "game.h"

/* A level was loaded (lv must stay loaded until the next call): read its
 * graph from nav (nullptr or size 0: none) or start building it. Returns
 * true if the file's graph was used. */
bool cpu_set_level(const Level *lv, const PlayerTuning *t, const uint8_t *nav, uint32_t size);
/* Build up to n more nodes of a graph cpu_set_level started; true when ready. */
bool cpu_build(int n);
bool cpu_ready(void);

/* Bots start over, seeded from seed (replays: a fixed seed). */
void cpu_seed(uint32_t seed);

/* In play, replace the input of each player in mask (bit per player) with
 * its bot's. A match started from the setup screen reseeds the bots from
 * the frame counter. */
void cpu_play(const Game *g, uint8_t mask, InputState in[MAX_PLAYERS]);

/* Game.cpus as a mask. */
uint8_t cpu_mask(const Game *g);

#endif
