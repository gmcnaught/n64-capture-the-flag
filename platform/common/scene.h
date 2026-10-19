/* Shared renderer for the host and N64 builds: draws a Game through a small
 * platform drawing interface, so both show the same pixels.
 *
 * Coordinates are sim/framebuffer pixels (0..VIEW_W, 0..VIEW_H = 640x240).
 * Every sheet is built 2x wide (tools/build_sprites.py), so blits are 1:1
 * and the N64 can use copy mode.
 */
#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>

#include "game.h"

/* Sprite sheets, loaded by the platform by name (build/assets/{host,n64}). */
enum { SPR_TILES, SPR_P1, SPR_P2, SPR_PAD, SPR_FLAG, SPR_BACKDROP, SPR_MOUNTAINS, SPR_HUD, SPR_FRUIT, SPR_FONT,
       SPR_SLASH, SPR_CONTROLS, SPR_COUNT };
extern const char *const scene_sprite_names[SPR_COUNT];

enum { FLIP_X = 1, FLIP_Y = 2 };

/* Text glyphs are 8x8 art px: 16 sim px wide, 8 tall. */
#define TEXT_CW 16
#define TEXT_CH 8

/* Action-safe margins: TVs and scalers can crop about 5% of each edge
 * (overscan), so the HUD stays inside them; the arena keeps walls there. */
#define SAFE_X 32
#define SAFE_Y 12

typedef struct {
    /* Draw a w x h rectangle of a sprite sheet from (sx, sy) at (x, y). */
    void (*blit)(void *ctx, int spr, int sx, int sy, int w, int h, int x, int y, int flip);
    /* blit with the texels' color multiplied by rgb (0xRRGGBB). */
    void (*blit_tint)(void *ctx, int spr, int sx, int sy, int w, int h, int x, int y, int flip,
                      uint32_t rgb);
    /* Fill a rectangle; color 0xRRGGBBAA, alpha-blended when AA < 0xff. */
    void (*fill)(void *ctx, int x, int y, int w, int h, uint32_t rgba);
    /* Text at (x, y), TEXT_CW x TEXT_CH per character. */
    void (*text)(void *ctx, int x, int y, const char *s);
    void *ctx;
} DrawApi;

/* Sky and drifting clouds, drawn first every frame. */
void scene_draw_sky(const Game *g, const DrawApi *d);
/* The parts that do not change during a game (mountains, trees, tile
 * layers, goal stands), over the sky with transparent gaps: the N64 draws
 * them once per level into a buffer and copies it every frame. */
void scene_draw_background(const Game *g, const DrawApi *d);
/* Everything else, over the background. debug: collision overlay and
 * hitboxes. */
void scene_draw(const Game *g, const DrawApi *d, int debug);

#endif
