/* Level (arena) data: collision grid, tile layers and entity spawns.
 *
 * Sim coordinates are 640x240 framebuffer pixels. The art uses square
 * pixels and an 8x8 tile grid; on a 4:3 display a 640x240 frame has
 * half-width pixels, so every art pixel is drawn 2 sim px wide and one
 * cell is TILE_W x TILE_H = 16 x 8 sim px: the arena is 40x30 cells.
 *
 * Binary format (big-endian, written by tools/ldtk_import.py):
 *   char     magic[4] = "LVL3" ("LVL2": no surfaces, also read)
 *   u16      cell_w, cell_h             size in cells
 *   u16      layer_count, entity_count
 *   u8       weather, music, backdrop, tileset (render/audio ids; backdrop: BACKDROP_*,
 *                                       tileset: platform/common/tilesets.h)
 *   u8       cells[cell_w * cell_h]     low nibble: collision value (CELL_*),
 *                                       high nibble: surface (SURFACE_*)
 *   (pad to an even offset)
 *   layer_count times:
 *     i16    parallax_x, parallax_y     Q8.8; unused (the camera is fixed)
 *     u16    tiles[cell_w * cell_h]     TILE_EMPTY or id | TILE_FLIP_*: cell of the
 *                                       tileset's sheet (tools/terrain.py)
 *   entity_count times (ENTITY_BYTES):
 *     u16    type                        ENT_*
 *     i16    x, y                        top-left, sim px
 *     u16    w, h                        sim px
 *     i16    x2, y2                      path end, top-left sim px (= x, y if none)
 *     i16    param                       type-specific
 *     u16    flag                        type-specific (FLAG_NONE if unused)
 *
 * The core reads only cells and entities; renderers read tile layers.
 */
#ifndef LEVEL_H
#define LEVEL_H

#include <stdint.h>

#define TILE_W 16
#define TILE_H 8
#define TILE_SHIFT_X 4
#define TILE_SHIFT_Y 3

enum {
    CELL_EMPTY  = 0,
    CELL_SOLID  = 1,
    CELL_ONEWAY = 2,
    CELL_HAZARD = 3,
    CELL_LADDER = 4,
};

enum {
    ENT_P1_START = 1,
    ENT_P2_START = 2,
    ENT_JUMP_PAD = 3,
    ENT_FLAG1    = 4,   /* team 1 (P1) flag at home, one per flag */
    ENT_FLAG2    = 5,   /* team 2 (P2) flag at home */
    ENT_GOAL1    = 6,   /* w x h zone where team 1 places team 2's flags */
    ENT_GOAL2    = 7,
    ENT_FRUIT_SPOT = 8, /* where a water fruit can appear (core/game.h) */
    ENT_WIND     = 9,   /* w x h zone of wind gusts (core/game.h WindZone): param peak
                         * force (Q8.8 px/frame, sign = first direction), flag cycle
                         * length and x2 gust length (frames), y2 bit 0 direction
                         * alternates, bit 1 drawn as dust, bits 2-14 phase (frames) */
};

/* Surfaces: what a solid or one-way cell's top is made of. */
enum { SURFACE_NONE = 0, SURFACE_ICE = 1 };

/* Backdrops (sky and scenery sheet pairs, tools/scenery.py BACKDROPS order):
 * the level's is the default, the title screen can pick another. */
enum { BACKDROP_FOREST, BACKDROP_DESERT, BACKDROP_MOON, BACKDROP_SKY, BACKDROP_COUNT };

#define TILE_EMPTY  0xffffu
#define TILE_ID     0x0fffu
#define TILE_FLIP_X 0x4000u
#define TILE_FLIP_Y 0x8000u

#define FLAG_NONE 0xffffu
#define LEVEL_HEADER_BYTES 16
#define ENTITY_BYTES 18

typedef struct {
    uint16_t type;
    int16_t x, y;
    uint16_t w, h;
    int16_t x2, y2;
    int16_t param;
    uint16_t flag;
} LevelEntity;

typedef struct Level {
    uint16_t cell_w, cell_h;
    /* One extra solid box (the other player, set by core/game.c around each
     * player_tick); solid_w == 0 means none. */
    int16_t solid_x, solid_y, solid_w, solid_h;
    int16_t px_w, px_h;
    uint16_t layer_count, entity_count;
    uint8_t weather, music, backdrop, tileset;
    uint16_t surfaces;          /* bit 1 << SURFACE_* for each surface the level has */
    const uint8_t *cells;
    const uint8_t *layers;      /* first layer header */
    const uint8_t *entities;    /* first entity record */
    const char *name;           /* shown on the title screen; set by the platform, or NULL */
} Level;

/* Returns 0 on success, nonzero if the data is malformed. Level keeps
 * pointers into data, which must outlive it. */
int level_load(Level *lv, const uint8_t *data, uint32_t size);

/* Collision value of a cell. Outside the level, sides and top are solid and
 * below the bottom is empty (falling out = pit). */
uint8_t level_cell(const Level *lv, int cx, int cy);

/* Surface of a cell (SURFACE_*); none outside the level. */
uint8_t level_surface(const Level *lv, int cx, int cy);

void level_entity(const Level *lv, int i, LevelEntity *out);

uint16_t level_tile(const Level *lv, int layer, int cx, int cy);

/* Box queries, sim px, box = [x, x+w) x [y, y+h). level_box_solid also
 * tests the extra solid box. */
int level_box_solid(const Level *lv, int x, int y, int w, int h);
int level_box_has(const Level *lv, int x, int y, int w, int h, uint8_t cell);

/* True if a cell is a platform: one-way, or the top cell of a ladder. */
int level_cell_platform(const Level *lv, int cx, int cy);

/* Hazard hit test against the visible part of each hazard cell: spikes
 * point away from an adjacent solid, and only that half of the cell hurts. */
int level_box_hazard(const Level *lv, int x, int y, int w, int h);

#endif
