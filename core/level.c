#include "level.h"

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Floor division by the cell size for possibly negative px coordinates. */
static int cell_x(int px) { return px >> TILE_SHIFT_X; }
static int cell_y(int px) { return px >> TILE_SHIFT_Y; }

static uint32_t layer_bytes(const Level *lv)
{
    return 4u + 2u * lv->cell_w * lv->cell_h;
}

int level_load(Level *lv, const uint8_t *data, uint32_t size)
{
    if (size < LEVEL_HEADER_BYTES || data[0] != 'L' || data[1] != 'V' || data[2] != 'L' ||
        data[3] != '2')
        return 1;
    *lv = (Level){0};
    lv->cell_w = be16(data + 4);
    lv->cell_h = be16(data + 6);
    lv->layer_count = be16(data + 8);
    lv->entity_count = be16(data + 10);
    lv->weather = data[12];
    lv->music = data[13];
    lv->backdrop = data[14];
    lv->flags = data[15];
    if (lv->cell_w == 0 || lv->cell_h == 0 || lv->cell_w > 1023 || lv->cell_h > 2047)
        return 2;
    lv->px_w = (int16_t)(lv->cell_w * TILE_W);
    lv->px_h = (int16_t)(lv->cell_h * TILE_H);

    uint32_t off = LEVEL_HEADER_BYTES;
    lv->cells = data + off;
    off += (uint32_t)lv->cell_w * lv->cell_h;
    off = (off + 1u) & ~1u;
    lv->layers = data + off;
    off += layer_bytes(lv) * lv->layer_count;
    lv->entities = data + off;
    off += (uint32_t)ENTITY_BYTES * lv->entity_count;
    return off == size ? 0 : 3;
}

uint8_t level_cell(const Level *lv, int cx, int cy)
{
    if (cx >= 0 && cx < lv->cell_w && cy >= 0 && cy < lv->cell_h)
        return lv->cells[cy * lv->cell_w + cx];
    return cy >= lv->cell_h ? CELL_EMPTY : CELL_SOLID;
}

void level_entity(const Level *lv, int i, LevelEntity *out)
{
    const uint8_t *p = lv->entities + ENTITY_BYTES * i;
    out->type = be16(p);
    out->x = (int16_t)be16(p + 2);
    out->y = (int16_t)be16(p + 4);
    out->w = be16(p + 6);
    out->h = be16(p + 8);
    out->x2 = (int16_t)be16(p + 10);
    out->y2 = (int16_t)be16(p + 12);
    out->param = (int16_t)be16(p + 14);
    out->flag = be16(p + 16);
}

uint16_t level_tile(const Level *lv, int layer, int cx, int cy)
{
    if (cx < 0 || cx >= lv->cell_w || cy < 0 || cy >= lv->cell_h)
        return TILE_EMPTY;
    const uint8_t *l = lv->layers + layer_bytes(lv) * (uint32_t)layer;
    return be16(l + 4 + 2 * (cy * lv->cell_w + cx));
}

int level_box_solid(const Level *lv, int x, int y, int w, int h)
{
    if (lv->solid_w && x < lv->solid_x + lv->solid_w && x + w > lv->solid_x &&
        y < lv->solid_y + lv->solid_h && y + h > lv->solid_y)
        return 1;
    return level_box_has(lv, x, y, w, h, CELL_SOLID);
}

int level_box_has(const Level *lv, int x, int y, int w, int h, uint8_t cell)
{
    int cx0 = cell_x(x), cx1 = cell_x(x + w - 1);
    int cy0 = cell_y(y), cy1 = cell_y(y + h - 1);
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
            if (level_cell(lv, cx, cy) == cell)
                return 1;
    return 0;
}

int level_cell_platform(const Level *lv, int cx, int cy)
{
    uint8_t c = level_cell(lv, cx, cy);
    if (c == CELL_ONEWAY)
        return 1;
    return c == CELL_LADDER && level_cell(lv, cx, cy - 1) != CELL_LADDER;
}

int level_box_hazard(const Level *lv, int x, int y, int w, int h)
{
    int cx0 = cell_x(x), cx1 = cell_x(x + w - 1);
    int cy0 = cell_y(y), cy1 = cell_y(y + h - 1);
    for (int cy = cy0; cy <= cy1; cy++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            if (level_cell(lv, cx, cy) != CELL_HAZARD)
                continue;
            /* Hurt area inside the cell, px relative to the cell origin: an
             * inset of 1/8 cell, or the half facing away from a solid. */
            int hx0 = TILE_W / 8, hy0 = TILE_H / 8, hx1 = TILE_W - TILE_W / 8, hy1 = TILE_H - TILE_H / 8;
            if (level_cell(lv, cx, cy + 1) == CELL_SOLID)       { hy0 = TILE_H / 2; hy1 = TILE_H; }
            else if (level_cell(lv, cx, cy - 1) == CELL_SOLID)  { hy0 = 0; hy1 = TILE_H / 2; }
            else if (level_cell(lv, cx - 1, cy) == CELL_SOLID)  { hx0 = 0; hx1 = TILE_W / 2; }
            else if (level_cell(lv, cx + 1, cy) == CELL_SOLID)  { hx0 = TILE_W / 2; hx1 = TILE_W; }
            int ox = cx * TILE_W, oy = cy * TILE_H;
            if (x < ox + hx1 && x + w > ox + hx0 && y < oy + hy1 && y + h > oy + hy0)
                return 1;
        }
    }
    return 0;
}
