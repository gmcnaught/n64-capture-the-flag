/* Integer-position body with Q16.16 remainder (technical design §3.2). */
#ifndef BODY_H
#define BODY_H

#include <stdint.h>

#include "fixed.h"
#include "level.h"

typedef struct {
    int16_t x, y;           /* authoritative position, whole px (top-left) */
    int16_t w, h;           /* hitbox size, px */
    fx rem_x, rem_y;        /* sub-pixel remainder, [-0.5, 0.5) px after a move */
    fx vx, vy;              /* velocity, px/frame */
} Body;

/* Move by amount; stops at solids. Returns 1 if a solid stopped it (the
 * remainder on that axis is then zeroed; the caller zeroes velocity). */
int body_move_x(Body *b, fx amount, const Level *lv);

/* As body_move_x. When platforms is nonzero, one-way platforms and ladder
 * tops also stop downward movement onto their top edge. */
int body_move_y(Body *b, fx amount, const Level *lv, int platforms);

/* Is there something to stand on one px below the body? */
int body_on_solid(const Body *b, const Level *lv);
int body_on_platform(const Body *b, const Level *lv);

/* Is the body touching a solid wall on side dir (-1 left, +1 right)? */
int body_wall(const Body *b, const Level *lv, int dir);

#endif
