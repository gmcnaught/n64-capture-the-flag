#include "body.h"

/* Split the whole px out of a remainder, rounding to nearest. */
static int take_pixels(fx *rem)
{
    int move = fx_round(*rem);
    *rem -= FX(move);
    return move;
}

int body_move_x(Body *b, fx amount, const Level *lv)
{
    b->rem_x += amount;
    int move = take_pixels(&b->rem_x);
    int step = move > 0 ? 1 : -1;
    while (move != 0) {
        if (level_box_solid(lv, b->x + step, b->y, b->w, b->h)) {
            b->rem_x = 0;
            return 1;
        }
        b->x = (int16_t)(b->x + step);
        move -= step;
    }
    return 0;
}

/* Would moving down one px land on a platform's top edge? */
static int platform_below(const Body *b, const Level *lv)
{
    int bottom = b->y + b->h;              /* first px row below the body */
    if (bottom & (TILE_H - 1))
        return 0;
    int cy = bottom >> TILE_SHIFT_Y;
    for (int cx = b->x >> TILE_SHIFT_X; cx <= (b->x + b->w - 1) >> TILE_SHIFT_X; cx++)
        if (level_cell_platform(lv, cx, cy))
            return 1;
    return 0;
}

int body_move_y(Body *b, fx amount, const Level *lv, int platforms)
{
    b->rem_y += amount;
    int move = take_pixels(&b->rem_y);
    int step = move > 0 ? 1 : -1;
    while (move != 0) {
        if (level_box_solid(lv, b->x, b->y + step, b->w, b->h) ||
            (step > 0 && platforms && platform_below(b, lv))) {
            b->rem_y = 0;
            return 1;
        }
        b->y = (int16_t)(b->y + step);
        move -= step;
    }
    return 0;
}

int body_on_solid(const Body *b, const Level *lv)
{
    return level_box_solid(lv, b->x, b->y + 1, b->w, b->h);
}

int body_on_platform(const Body *b, const Level *lv)
{
    return platform_below(b, lv);
}

int body_wall(const Body *b, const Level *lv, int dir)
{
    return level_box_solid(lv, b->x + dir, b->y, b->w, b->h);
}
