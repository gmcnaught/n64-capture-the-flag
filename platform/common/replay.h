/* Replay files: text, one run-length line per input change.
 *
 *   # comment
 *   level arena.lvl
 *   abilities 7
 *   tune jump_speed 0x00068000      (fields that differ from the defaults)
 *   30 R                            (frames, P1 buttons LRUDJXASC or -, [sx sy])
 *   12 RJ / LC                      (... / P2 buttons [sx sy]; C = connected)
 *   hash 1a2b3c4d                   (expected game_hash after the last frame)
 *   kos 1 0                         (optional: expected knockouts P1, P2)
 */
#ifndef REPLAY_H
#define REPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "game.h"

typedef struct {
    InputState in[2];
} ReplayFrame;

typedef struct {
    char level[64];
    uint8_t abilities;
    PlayerTuning tuning;
    ReplayFrame *frames;
    size_t count, cap;
    int has_hash, has_kos;
    uint32_t hash;
    int kos[TEAM_COUNT];
    char comments[1024];            /* leading # lines, kept on save */
} Replay;

void replay_init(Replay *r, const char *level, uint8_t abilities, const PlayerTuning *t);
void replay_push(Replay *r, const InputState *p1, const InputState *p2);
void replay_free(Replay *r);
int replay_load(Replay *r, const char *path);       /* 0 on success */
int replay_save(const Replay *r, const char *path); /* 0 on success */

/* Start a game for a replay in its level (loaded by the caller). */
void replay_start(const Replay *r, Game *g, const Level *lv);

#endif
