/* Replay files: text, one run-length line per input change.
 *
 *   # comment
 *   level arena.lvl
 *   players 4                       (optional: Game.player_count, default 2)
 *   cpu 2                           (optional: bit per player played by a bot,
 *                                    platform/common/cpu.h, seeded with 1; the
 *                                    file's input for those players is not used)
 *   abilities 7
 *   tune jump_speed 0x00068000      (fields that differ from the defaults)
 *   30 R                            (frames, P1 buttons LRUDJXASC or -, [sx sy])
 *   12 RJ / LC                      (... / P2 buttons [sx sy]; C = connected; up to
 *                                    P4, missing ones idle)
 *   hash 1a2b3c4d                   (expected game_hash after the last frame)
 *   kos 1 0                         (optional: expected knockouts P1, P2 [, P3, P4])
 */
#ifndef REPLAY_H
#define REPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "game.h"

typedef struct {
    InputState in[MAX_PLAYERS];
} ReplayFrame;

typedef struct {
    char level[64];
    uint8_t abilities;
    uint8_t players;                /* Game.player_count at the start */
    uint8_t cpu;                    /* players played by bots (bit per player) */
    PlayerTuning tuning;
    ReplayFrame *frames;
    size_t count, cap;
    bool has_hash, has_kos;
    uint32_t hash;
    int kos[MAX_PLAYERS];
    char comments[1024];            /* leading # lines, kept on save */
} Replay;

void replay_init(Replay *r, const char *level, uint8_t abilities, const PlayerTuning *t);
void replay_push(Replay *r, const InputState in[MAX_PLAYERS]);
void replay_free(Replay *r);
[[nodiscard]] int replay_load(Replay *r, const char *path);       /* 0 on success */
[[nodiscard]] int replay_save(const Replay *r, const char *path); /* 0 on success */

/* Start a game for a replay in its level (loaded by the caller). */
void replay_start(const Replay *r, Game *g, const Level *lv);

#endif
