#include "selftest.h"
#include "fixed.h"

/* Minimal stand-in for the M1 Body: falls under gravity, bounces off a
 * floor at y = 200 by jumping, and walks back and forth between x = 16 and
 * x = 300. Exercises negative remainders and the round-to-nearest split. */
typedef struct {
    int16_t x, y;
    fx rem_x, rem_y;
    fx vx, vy;
} TestBody;

static int16_t take_pixels(fx *rem, fx amount)
{
    *rem += amount;
    int16_t move = fx_round(*rem);
    *rem -= FX(move);
    return move;
}

static uint32_t mix(uint32_t h, uint32_t v)
{
    h = (h << 5) | (h >> 27);
    return h ^ v;
}

uint32_t core_selftest(void)
{
    TestBody b = { 16, 0, 0, 0, 0x00018000, 0 }; /* vx = 1.5 px/frame */
    const fx gravity = 0x3800;                   /* 0.21875 px/frame^2 */
    const fx jump = -FX(5) - 0x4000;             /* -5.25 px/frame */
    const fx max_fall = FX(6);
    uint32_t h = 0x12345678u;

    for (uint16_t frame = 0; frame < 1200; frame++) {
        b.vy += gravity;
        if (b.vy > max_fall)
            b.vy = max_fall;

        b.x += take_pixels(&b.rem_x, b.vx);
        if (b.x >= 300 || b.x <= 16) {
            b.vx = -b.vx;
            b.rem_x = 0;
        }

        b.y += take_pixels(&b.rem_y, b.vy);
        if (b.y >= 200) {
            b.y = 200;
            b.rem_y = 0;
            b.vy = jump;
        }

        h = mix(h, ((uint32_t)(uint16_t)b.x << 16) | (uint16_t)b.y);
    }
    return h;
}
