#include "body.h"

/* Split the whole px out of a remainder, rounding to nearest. */
static int take_pixels(fx *rem)
{
    int move = fx_round(*rem);
    *rem -= FX(move);
    return move;
}

bool body_move_x(Body *b, fx amount, const Level *lv)
{
    b->rem_x += amount;
    int move = take_pixels(&b->rem_x);
    int step = move > 0 ? 1 : -1;
    while (move != 0) {
        if (level_box_solid(lv, b->x + step, b->y, b->w, b->h)) {
            b->rem_x = 0;
            return true;
        }
        b->x = (int16_t)(b->x + step);
        move -= step;
    }
    return false;
}

/* Would moving down one px land on a platform's top edge (a one-way cell,
 * a ladder top or a one-way solid box)? */
static bool platform_below(const Body *b, const Level *lv)
{
    int bottom = b->y + b->h;              /* first px row below the body */
    for (int i = 0; i < lv->solid_count; i++) {
        const Solid *s = &lv->solids[i];
        if (s->active && s->oneway && bottom == s->y && b->x < s->x + s->w && b->x + b->w > s->x)
            return true;
    }
    if (bottom & (TILE_H - 1))
        return false;
    int cy = bottom >> TILE_SHIFT_Y;
    for (int cx = b->x >> TILE_SHIFT_X; cx <= (b->x + b->w - 1) >> TILE_SHIFT_X; cx++)
        if (level_cell_platform(lv, cx, cy))
            return true;
    return false;
}

bool body_move_y(Body *b, fx amount, const Level *lv, bool platforms)
{
    b->rem_y += amount;
    int move = take_pixels(&b->rem_y);
    int step = move > 0 ? 1 : -1;
    while (move != 0) {
        if (level_box_solid(lv, b->x, b->y + step, b->w, b->h) ||
            (step > 0 && platforms && platform_below(b, lv))) {
            b->rem_y = 0;
            return true;
        }
        b->y = (int16_t)(b->y + step);
        move -= step;
    }
    return false;
}

bool body_on_solid(const Body *b, const Level *lv)
{
    return level_box_solid(lv, b->x, b->y + 1, b->w, b->h);
}

bool body_on_platform(const Body *b, const Level *lv)
{
    return platform_below(b, lv);
}

bool body_wall(const Body *b, const Level *lv, int dir)
{
    return level_box_solid(lv, b->x + dir, b->y, b->w, b->h);
}
