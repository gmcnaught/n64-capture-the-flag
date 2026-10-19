#include "scene.h"

#include <stdio.h>
#include <string.h>

#include "backdrops.h"
#include "font_glyphs.h"
#include "tilesets.h"

static const char *const sprite_names[SPR_COUNT] = {
    "tiles", "p1", "p2", "jumppad", "flag", "sky", "scenery", "hud", "fruit", "font", "slash",
    "controls",
};

void scene_sheet_name(int spr, int backdrop, int tileset, char *out, int size)
{
    if (spr == SPR_TILES)
        snprintf(out, (size_t)size, "%s", tileset_sheets[tileset]);
    else if (spr == SPR_SKY || spr == SPR_SCENERY)
        snprintf(out, (size_t)size, "%s_%s", sprite_names[spr], backdrop_names[backdrop]);
    else
        snprintf(out, (size_t)size, "%s", sprite_names[spr]);
}

int scene_tileset(const Level *lv)
{
    return lv->tileset < TILESET_COUNT ? lv->tileset : 0;
}

/* Sheet layouts, sim px (tools/build_sprites.py). */
#define FLAG_SPR_W 32           /* 9 frames of 32x16; row 0 team 1, row 1 team 2, */
#define FLAG_OUTLINE_ROW 2      /* row 2: dotted outline for empty goal stands */
#define FLAG_SPR_H 16
#define FLAG_FRAMES 9
#define PAD_W 16                /* 4 frames of 16x16: rest, half, up, squashed */
#define PAD_H 16
#define SKY_W 640               /* "sky" sheets: 640 x backdrop_sky_h (tools/scenery.py); */
                                /* "scenery" sheets are the whole screen, 640 x 240 */
#define HEART_W 16              /* full, 3/4, 1/2, empty */
#define HEART_H 8

static void tile(const DrawApi *d, uint16_t t, int x, int y)
{
    int id = t & TILE_ID;
    int flip = ((t & TILE_FLIP_X) ? FLIP_X : 0) | ((t & TILE_FLIP_Y) ? FLIP_Y : 0);
    d->blit(d->ctx, SPR_TILES, id % TILESET_COLS * TILE_W, id / TILESET_COLS * TILE_H,
            TILE_W, TILE_H, x, y, flip);
}

static void outline(const DrawApi *d, int x, int y, int w, int h, uint32_t c)
{
    d->fill(d->ctx, x, y, w, 1, c);
    d->fill(d->ctx, x, y + h - 1, w, 1, c);
    d->fill(d->ctx, x, y, 2, h, c);
    d->fill(d->ctx, x + w - 2, y, 2, h, c);
}

/* ---- backdrop ---------------------------------------------------------------- */

/* Sky: a screen-wide picture that tiles horizontally, drifting left 1 px
 * every 4 frames (render only: it reads the frame counter and never feeds
 * back into the sim). The scenery below covers the rest of the screen. */
void scene_draw_sky(const Game *g, const DrawApi *d)
{
    int s = (int)(g->frame / 4 % SKY_W), h = backdrop_sky_h[g->backdrop];
    for (int x = -s; x < VIEW_W; x += SKY_W)
        d->blit(d->ctx, SPR_SKY, 0, 0, SKY_W, h, x, 0, 0);
}

static void draw_backdrop(const DrawApi *d)
{
    d->blit(d->ctx, SPR_SCENERY, 0, 0, VIEW_W, VIEW_H, 0, 0, 0);
}

static void draw_layers(const Game *g, const DrawApi *d)
{
    const Level *lv = &g->lv;
    for (int layer = 0; layer < lv->layer_count; layer++)
        for (int cy = 0; cy < lv->cell_h; cy++)
            for (int cx = 0; cx < lv->cell_w; cx++) {
                uint16_t t = level_tile(lv, layer, cx, cy);
                if (t != TILE_EMPTY)
                    tile(d, t, cx * TILE_W, cy * TILE_H);
            }
}

/* Goals: one team-colored stand per flag the team must capture, at the
 * spots where core/game.c lays placed flags out. */
static const uint32_t TEAM_COLOR[TEAM_COUNT] = {0xe03c5aff, 0x3c78e0ff};

static void draw_goals(const Game *g, const DrawApi *d)
{
    for (int t = 0; t < TEAM_COUNT; t++) {
        int x = g->goal_x[t], y = g->goal_y[t], w = g->goal_w[t], h = g->goal_h[t];
        if (!w)
            continue;
        for (int n = 0; n < FLAGS_PER_TEAM; n++) {
            int sx = x + w * (n + 1) / (FLAGS_PER_TEAM + 1) - FLAG_W / 2;
            d->fill(d->ctx, sx, y + h - 3, FLAG_W, 3, TEAM_COLOR[t]);
            d->fill(d->ctx, sx + FLAG_W / 2 - 2, y + h - 3, 4, 2, 0x2a1e2aff);   /* socket */
        }
    }
}

/* ---- objects -------------------------------------------------------------- */

static void draw_flag(const Game *g, const DrawApi *d, int i)
{
    const Flag *f = &g->flags[i];
    int frame = (int)((g->frame / 6 + (uint32_t)i * 3) % FLAG_FRAMES);
    int flip = f->state == FS_CARRIED && g->players[f->holder].facing < 0 ? FLIP_X : 0;
    d->blit(d->ctx, SPR_FLAG, frame * FLAG_SPR_W, f->team * FLAG_SPR_H, FLAG_SPR_W, FLAG_SPR_H,
            f->body.x + FLAG_W / 2 - FLAG_SPR_W / 2, f->body.y, flip);
}

/* Flags on the ground, at home and in goals; carried ones are drawn with
 * their carrier. Goal stands without a flag show a dotted outline. */
static void draw_flags(const Game *g, const DrawApi *d)
{
    for (int t = 0; t < TEAM_COUNT; t++) {
        int placed = 0;
        for (int i = 0; i < g->flag_count; i++)
            placed += g->flags[i].state == FS_PLACED && g->flags[i].holder == t;
        for (int n = placed; n < FLAGS_PER_TEAM && g->goal_w[t]; n++) {
            int x = g->goal_x[t] + g->goal_w[t] * (n + 1) / (FLAGS_PER_TEAM + 1) - FLAG_W / 2;
            d->blit(d->ctx, SPR_FLAG, 0, FLAG_OUTLINE_ROW * FLAG_SPR_H, FLAG_SPR_W, FLAG_SPR_H,
                    x + FLAG_W / 2 - FLAG_SPR_W / 2, g->goal_y[t] + g->goal_h[t] - FLAG_H, 0);
        }
    }
    for (int i = 0; i < g->flag_count; i++)
        if (g->flags[i].state != FS_CARRIED)
            draw_flag(g, d, i);
}

static void draw_pads(const Game *g, const DrawApi *d)
{
    static const uint8_t seq[] = {3, 2, 2, 1, 0};
    for (int i = 0; i < g->pad_count; i++) {
        const JumpPad *pad = &g->pads[i];
        int frame = pad->timer ? seq[(PAD_ANIM_FRAMES - pad->timer) * 5 / PAD_ANIM_FRAMES] : 0;
        d->blit(d->ctx, SPR_PAD, frame * PAD_W, 0, PAD_W, PAD_H,
                pad->x + (TILE_W - PAD_W) / 2, pad->y + TILE_H - PAD_H, 0);
    }
}

/* Water fruit (bobbing) and the stream: translucent cells over the
 * players, so caught ones show through; a light front. */
static void draw_water(const Game *g, const DrawApi *d)
{
    if (g->fruit_active) {
        int x = g->fruit_spot_x[g->fruit_spot], y = g->fruit_spot_y[g->fruit_spot];
        int bob = (g->frame >> 4) & 1;
        if (g->fruit_timer > 120 || (g->frame & 4))     /* blink before it goes */
            d->blit(d->ctx, SPR_FRUIT, 0, 0, TILE_W, TILE_H, x, y - bob, 0);
    }
    for (int i = g->water_len - 1; i >= 0; i--) {
        int x = g->water_cx[i] * TILE_W, y = g->water_cy[i] * TILE_H;
        d->fill(d->ctx, x, y, TILE_W, TILE_H, 0x2f7fd8b8);
        if (i == 0)
            d->fill(d->ctx, x, y, TILE_W, 2, 0xc8ecffe0);
    }
}

#define DASH_GHOSTS 3
#define DASH_GHOST_STEP 22      /* px between afterimages: two frames of dash_speed */
#define SLASH_SIDE_W 32         /* "slash" sheet (tools/effects.py), sim px */
#define SLASH_SIDE_H 24
#define SLASH_UP_W 48
#define SLASH_UP_H 16
static const uint32_t GHOST_TINT[TEAM_COUNT][DASH_GHOSTS] = {
    {0xe06060, 0xa04040, 0x682a2a}, {0x5888e0, 0x3c5ca0, 0x283c68},
};
static const uint32_t SLASH_TINT[TEAM_COUNT] = {0xffa0a0, 0xa8d0ff};

static void draw_player(const Game *g, const DrawApi *d, int t, int spr)
{
    const Player *p = &g->players[t];
    const Body *b = &p->body;
    if (g->carrying[t] >= 0)
        draw_flag(g, d, g->carrying[t]);    /* behind the carrier */
    if (p->dead || (p->invuln & 4))
        return;
    int cx = p->facing < 0 ? PLAYER_SPRITE_W - PLAYER_SPRITE_CX : PLAYER_SPRITE_CX;
    int x = b->x + b->w / 2 - cx, y = b->y + b->h - PLAYER_SPRITE_FOOT;
    int flip = p->facing < 0 ? FLIP_X : 0;
    int sx = p->anim_frame * PLAYER_SPRITE_W, sy = p->anim * PLAYER_SPRITE_H;
    if (p->state == PS_DASH) {
        /* Afterimages in the team color, fading, along the dash so far
         * (one per two frames, so none where the dash has not been). Each
         * shows every other frame, alternating, so they read as see-through
         * (render only: reads the frame counter). */
        int age = g->tuning.dash_frames - p->dash_timer;
        for (int k = DASH_GHOSTS; k >= 1; k--)
            if (age >= 2 * k && ((g->frame + (uint32_t)k) & 1))
                d->blit_tint(d->ctx, spr, sx, sy, PLAYER_SPRITE_W, PLAYER_SPRITE_H,
                             x - p->dash_dir * k * DASH_GHOST_STEP, y, flip, GHOST_TINT[t][k - 1]);
    }
    d->blit(d->ctx, spr, sx, sy, PLAYER_SPRITE_W, PLAYER_SPRITE_H, x, y, flip);
}

/* Attack slash while the hitbox is out: 3 frames over attack_active. */
static void draw_slash(const Game *g, const DrawApi *d, int t)
{
    const Player *p = &g->players[t];
    const Body *b = &p->body;
    int active = g->tuning.attack_active > 0 ? g->tuning.attack_active : 1;
    int age = g->tuning.attack_frames - p->attack_timer;
    if (p->dead || !p->attack_timer || age >= active)
        return;
    int f = age * 3 / active;
    switch (p->attack_dir) {
    case ATK_UP:
    case ATK_DOWN:
        d->blit_tint(d->ctx, SPR_SLASH, f * SLASH_UP_W, SLASH_SIDE_H, SLASH_UP_W, SLASH_UP_H,
                     b->x + b->w / 2 - SLASH_UP_W / 2,
                     p->attack_dir == ATK_UP ? b->y + 2 - SLASH_UP_H : b->y + b->h - 2,
                     p->attack_dir == ATK_DOWN ? FLIP_Y : 0, SLASH_TINT[t]);
        break;
    default:
        d->blit_tint(d->ctx, SPR_SLASH, f * SLASH_SIDE_W, 0, SLASH_SIDE_W, SLASH_SIDE_H,
                     p->facing > 0 ? b->x + b->w - 6 : b->x + 6 - SLASH_SIDE_W,
                     b->y + b->h / 2 - SLASH_SIDE_H / 2, p->facing < 0 ? FLIP_X : 0,
                     SLASH_TINT[t]);
        break;
    }
}

/* ---- HUD -------------------------------------------------------------------- */

static void draw_hud(const Game *g, const DrawApi *d)
{
    char l[16];
    for (int t = 0; t < TEAM_COUNT; t++) {
        const Player *p = &g->players[t];
        /* P1 top-left, P2 top-right, inside the safe area: hearts, then the
         * rounds won. */
        int x0 = t == 0 ? SAFE_X : VIEW_W - SAFE_X - PLAYER_HEALTH * (HEART_W + 4);
        for (int i = 0; i < PLAYER_HEALTH; i++)
            d->blit(d->ctx, SPR_HUD, (i < p->health ? 0 : 3) * HEART_W, 0, HEART_W, HEART_H,
                    x0 + i * (HEART_W + 4), SAFE_Y, 0);
        snprintf(l, sizeof l, "%u", (unsigned)g->wins[t]);
        int tx = t == 0 ? x0 + PLAYER_HEALTH * (HEART_W + 4) + 8
                        : x0 - 8 - (int)strlen(l) * TEXT_CW;
        d->text(d->ctx, tx, SAFE_Y, l);
    }
}

/* ---- display font ------------------------------------------------------------ */

enum { FONT_MEDIUM, FONT_LARGE, FONT_SMALL };   /* font_sizes[] in font_glyphs.h */

#define TINT_WHITE 0xffffff
#define TINT_GOLD  0xffd040
#define TINT_DIM   0xa8b0c8
static const uint32_t TEAM_TINT[TEAM_COUNT] = {0xff5a5a, 0x60a8ff};

/* Height of a line of text, sim px (fill and outline; the shadow hangs below). */
static int font_h(int size)
{
    return font_sizes[size].h + 2;
}

/* Glyph i's box (fill, outline and shadow), which is also the pen advance,
 * sim px. */
static int font_advance(int i, int size)
{
    return (font_glyph_w[size][i] + 2 + font_sizes[size].shadow) * 2;
}

static int font_index(char c)
{
    const char *p = c ? strchr(FONT_CHARS, c) : NULL;
    return p ? (int)(p - FONT_CHARS) : 0;  /* unknown: space */
}

static int font_width(const char *s, int size)
{
    int w = 0;
    for (; *s; s++)
        w += font_advance(font_index(*s), size);
    return w;
}

/* Text with its top-left at (x, y). wave: letters bob up and down in a
 * ripple running along the line (render only: reads the frame counter). */
static void font_text(const Game *g, const DrawApi *d, int x, int y, const char *s, int size,
                      uint32_t tint, int wave)
{
    static const int8_t bob[16] = {0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1};
    int sc = font_sizes[size].h / 9, cols = font_sizes[size].cols;     /* bob: 1, 2 or 3 art px */
    int cw = font_sizes[size].cell_w, ch = font_sizes[size].cell_h;
    for (int n = 0; *s; s++, n++) {
        int i = font_index(*s);
        int dy = wave ? bob[(g->frame / 3 + n * 2) & 15] * sc / 3 : 0;
        if (i)
            d->blit_tint(d->ctx, SPR_FONT, i % cols * cw, font_sizes[size].y + i / cols * ch,
                         font_advance(i, size), ch, x, y + dy, 0, tint);
        x += font_advance(i, size);
    }
}

static void font_center(const Game *g, const DrawApi *d, int y, const char *s, int size,
                        uint32_t tint, int wave)
{
    font_text(g, d, (VIEW_W - font_width(s, size)) / 2, y, s, size, tint, wave);
}

/* ---- screens (title, pause menu, controls, round winner, countdown) ------------ */

/* The game shows through, darkened. */
static void dim(const DrawApi *d)
{
    d->fill(d->ctx, 0, 0, VIEW_W, VIEW_H, 0x000000a8);
}

/* Title screen row: a small label right-aligned to x, the value from
 * x + 16; the selected row is gold, with arrows that nudge outward if the
 * value can change (choices > 1). */
static void title_row(const Game *g, const DrawApi *d, int x, int y, const char *label,
                      const char *value, int sel, int choices)
{
    int sw = font_width(label, FONT_SMALL), mh = font_h(FONT_MEDIUM);
    font_text(g, d, x - sw, y + (mh - font_h(FONT_SMALL)) / 2, label, FONT_SMALL,
              sel ? TINT_WHITE : TINT_DIM, 0);
    int i = font_index('>'), cols = font_sizes[FONT_MEDIUM].cols, aw = font_advance(i, FONT_MEDIUM);
    int vx = x + 16 + aw + 8, vw = font_width(value, FONT_MEDIUM);
    font_text(g, d, vx, y, value, FONT_MEDIUM, sel ? TINT_GOLD : TINT_DIM, 0);
    if (!sel || choices < 2)
        return;
    int nudge = ((g->frame >> 3) & 1) * 4;
    for (int side = 0; side < 2; side++)    /* "<" is ">" mirrored */
        d->blit_tint(d->ctx, SPR_FONT, i % cols * font_sizes[FONT_MEDIUM].cell_w,
                     font_sizes[FONT_MEDIUM].y + i / cols * font_sizes[FONT_MEDIUM].cell_h, aw,
                     font_sizes[FONT_MEDIUM].cell_h, side ? vx + vw + 8 + nudge : x + 16 - nudge, y,
                     side ? 0 : FLIP_X, TINT_GOLD);
}

static void draw_title(const Game *g, const DrawApi *d)
{
    dim(d);
    int lh = font_h(FONT_LARGE);
    font_center(g, d, 30, "CAPTURE", FONT_LARGE, TINT_GOLD, 1);
    font_center(g, d, 30 + lh + 6, "THE FLAG", FONT_LARGE, TINT_GOLD, 1);
    /* RED VS BLUE in the team colors. */
    int w_red = font_width("RED ", FONT_MEDIUM), w_vs = font_width("VS ", FONT_MEDIUM);
    int x = (VIEW_W - w_red - w_vs - font_width("BLUE", FONT_MEDIUM)) / 2, y = 106;
    font_text(g, d, x, y, "RED ", FONT_MEDIUM, TEAM_TINT[0], 0);
    font_text(g, d, x + w_red, y, "VS ", FONT_MEDIUM, TINT_DIM, 0);
    font_text(g, d, x + w_red + w_vs, y, "BLUE", FONT_MEDIUM, TEAM_TINT[1], 0);
    /* Stage: the level's name up to its extension, upper case. */
    char stage[24], backdrop[16];
    const char *n = g->lv.name ? g->lv.name : "ARENA";
    int k = 0;
    for (; n[k] && n[k] != '.' && k < (int)sizeof stage - 1; k++)
        stage[k] = (char)(n[k] >= 'a' && n[k] <= 'z' ? n[k] - 'a' + 'A' : n[k]);
    stage[k] = 0;
    for (k = 0; backdrop_names[g->backdrop][k] && k < (int)sizeof backdrop - 1; k++)
        backdrop[k] = (char)(backdrop_names[g->backdrop][k] - 'a' + 'A');
    backdrop[k] = 0;
    title_row(g, d, 250, 140, "STAGE", stage, g->title_sel == TITLE_STAGE, g->stage_count);
    title_row(g, d, 250, 164, "BACKGROUND", backdrop, g->title_sel == TITLE_BACKDROP,
              BACKDROP_COUNT);
    if ((g->frame / 30) & 1 || g->frame < 30)
        font_center(g, d, 200, "PRESS START", FONT_MEDIUM, TINT_WHITE, 0);
}

static void draw_pause(const Game *g, const DrawApi *d)
{
    static const char *const items[MENU_COUNT] = {"RESUME", "CONTROLS", "RESTART", "QUIT TO TITLE"};
    dim(d);
    font_center(g, d, 44, "PAUSED", FONT_LARGE, TINT_GOLD, 0);
    /* Items left-aligned in a centered column; the cursor nudges right and
     * back on the selected one. */
    int w = 0;
    for (int i = 0; i < MENU_COUNT; i++) {
        int iw = font_width(items[i], FONT_MEDIUM);
        if (iw > w)
            w = iw;
    }
    int x = (VIEW_W - w) / 2, cursor_w = font_width("> ", FONT_MEDIUM);
    for (int i = 0; i < MENU_COUNT; i++) {
        int y = 100 + i * (font_h(FONT_MEDIUM) + 8);
        int sel = i == g->menu_sel;
        if (sel)
            font_text(g, d, x - cursor_w + ((g->frame >> 3) & 1) * 4, y, ">", FONT_MEDIUM,
                      TINT_GOLD, 0);
        font_text(g, d, x, y, items[i], FONT_MEDIUM, sel ? TINT_GOLD : TINT_DIM, 0);
    }
}

/* CONTROLS screen: the N64 controller (tools/controls.py) with leader lines
 * from its parts to two-line labels (part, action), and tips below. */
#define PAD_X 192               /* picture's top-left, sim px (256x84) */
#define PAD_Y 46
#define LABEL_GAP 16            /* picture edge to labels */
#define TINT_PART 0xd0d4e0
#define TINT_A 0x6c9cff
#define TINT_B 0x60d880

typedef struct {
    int8_t ax, ay, r;           /* part center and radius, art px (tools/controls.py) */
    uint8_t right;              /* label right of the picture, else left */
    uint8_t y;                  /* leader's y at the label (between its lines), sim px */
    uint32_t tint;              /* part name color */
    const char *part, *action;
} Callout;

static const Callout callouts[] = {
    {64, 26, 3, 0, 44, 0xff6060, "START", "PAUSE"},
    {24, 30, 9, 0, 76, TINT_PART, "D-PAD", "MOVE"},
    {64, 52, 9, 0, 108, TINT_PART, "STICK", "MOVE"},
    {64, 72, 4, 0, 140, TINT_PART, "Z", "DASH"},
    {108, 5, 4, 1, 51, TINT_PART, "R", "DASH"},
    {98, 44, 6, 1, 90, TINT_A, "A", "JUMP"},
    {88, 34, 5, 1, 122, TINT_B, "B", "ATTACK"},
};

/* A light line with a dark edge, horizontal or vertical, between two points. */
static void leader(const DrawApi *d, int x0, int y0, int x1, int y1)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 == y1) {
        d->fill(d->ctx, x0, y0 - 1, x1 - x0 + 1, 3, 0x141024ff);
        d->fill(d->ctx, x0, y0, x1 - x0 + 1, 1, 0xf0f0f8ff);
    } else {
        d->fill(d->ctx, x0 - 2, y0 - 1, 6, y1 - y0 + 3, 0x141024ff);
        d->fill(d->ctx, x0, y0, 2, y1 - y0 + 1, 0xf0f0f8ff);
    }
}

static void draw_controls(const Game *g, const DrawApi *d)
{
    static const char *const tips[] = {
        "A IN THE AIR: DOUBLE JUMP",
        "A AGAINST A WALL: WALL JUMP",
        "DOWN + A: DROP THROUGH A PLATFORM",
        "UP OR DOWN + B: AIM THE ATTACK",
        "CARRYING A FLAG: NO DASH OR ATTACK",
    };
    int th = font_h(FONT_SMALL);
    dim(d);
    font_center(g, d, 6, "CONTROLS", FONT_MEDIUM, TINT_GOLD, 0);
    int bw = font_width("B BACK", FONT_SMALL);      /* any of A, B, Start goes back */
    font_text(g, d, VIEW_W - SAFE_X - bw, 10, "B", FONT_SMALL, TINT_B, 0);
    font_text(g, d, VIEW_W - SAFE_X - bw + font_width("B", FONT_SMALL), 10, " BACK", FONT_SMALL,
              TINT_WHITE, 0);
    for (unsigned i = 0; i < sizeof callouts / sizeof callouts[0]; i++) {
        const Callout *c = &callouts[i];
        int px = PAD_X + c->ax * 2, py = PAD_Y + c->ay, y = c->y;
        int lx = c->right ? PAD_X + 256 + LABEL_GAP - 4 : PAD_X - LABEL_GAP + 4;
        if (py == y) {          /* straight across, from the part's edge */
            leader(d, c->right ? px + c->r * 2 : px - c->r * 2, y, lx, y);
        } else {                /* down or up to the label's y, then across */
            leader(d, px, py + (y > py ? c->r : -c->r), px, y);
            leader(d, px, y, lx, y);
        }
    }
    d->blit(d->ctx, SPR_CONTROLS, 0, 0, 256, 84, PAD_X, PAD_Y, 0);
    /* Labels at the leader ends: the part above the line, the action below. */
    for (unsigned i = 0; i < sizeof callouts / sizeof callouts[0]; i++) {
        const Callout *c = &callouts[i];
        int wp = font_width(c->part, FONT_SMALL), wa = font_width(c->action, FONT_SMALL);
        int x = c->right ? PAD_X + 256 + LABEL_GAP : PAD_X - LABEL_GAP;
        font_text(g, d, c->right ? x : x - wp, c->y - th - 1, c->part, FONT_SMALL, c->tint, 0);
        font_text(g, d, c->right ? x : x - wa, c->y + 2, c->action, FONT_SMALL, TINT_WHITE, 0);
    }
    for (unsigned i = 0; i < sizeof tips / sizeof tips[0]; i++)
        font_center(g, d, 162 + (int)i * 13, tips[i], FONT_SMALL, TINT_DIM, 0);
}

/* Round countdown over the arena: 3, 2, 1, then GO! into play. */
static void draw_countdown(const Game *g, const DrawApi *d)
{
    char n[2] = {0, 0};
    int y = (VIEW_H - font_h(FONT_LARGE)) / 2 - 8;
    if (g->countdown > COUNTDOWN_GO) {
        n[0] = (char)('1' + (g->countdown - COUNTDOWN_GO - 1) / COUNTDOWN_STEP);
        font_center(g, d, y, n, FONT_LARGE, TINT_GOLD, 0);
    } else {
        font_center(g, d, y, "GO!", FONT_LARGE, 0x70e070, 1);
    }
}

static void draw_winner(const Game *g, const DrawApi *d)
{
    int t = g->winner - 1;
    char a[16], b[16];
    dim(d);
    font_center(g, d, 70, t == 0 ? "RED WINS!" : "BLUE WINS!", FONT_LARGE, TEAM_TINT[t], 1);
    /* Rounds won: RED n - m BLUE, each side in its team color. */
    snprintf(a, sizeof a, "RED %u ", (unsigned)g->wins[0]);
    snprintf(b, sizeof b, " %u BLUE", (unsigned)g->wins[1]);
    int wa = font_width(a, FONT_MEDIUM), wd = font_width("-", FONT_MEDIUM);
    int x = (VIEW_W - wa - wd - font_width(b, FONT_MEDIUM)) / 2, y = 70 + font_h(FONT_LARGE) + 20;
    font_text(g, d, x, y, a, FONT_MEDIUM, TEAM_TINT[0], 0);
    font_text(g, d, x + wa, y, "-", FONT_MEDIUM, TINT_DIM, 0);
    font_text(g, d, x + wa + wd, y, b, FONT_MEDIUM, TEAM_TINT[1], 0);
}

static void draw_debug(const Game *g, const DrawApi *d)
{
    static const uint32_t colors[5] = {0, 0xe8f0ff46, 0x9fb4d846, 0xe03c3c46, 0xff9a1f46};
    const Level *lv = &g->lv;
    for (int y = 0; y < lv->cell_h; y++)
        for (int x = 0; x < lv->cell_w; x++) {
            uint8_t c = level_cell(lv, x, y);
            if (c && c < 5)
                d->fill(d->ctx, x * TILE_W, y * TILE_H, TILE_W, TILE_H, colors[c]);
        }
    for (int i = 0; i < g->pad_count; i++)
        outline(d, g->pads[i].x + PAD_HIT_X, g->pads[i].y + PAD_HIT_Y, TILE_W - 2 * PAD_HIT_X,
                TILE_H - PAD_HIT_Y, 0xffa000ff);
    for (int i = 0; i < g->flag_count; i++)
        outline(d, g->flags[i].body.x, g->flags[i].body.y, FLAG_W, FLAG_H, 0xffff60ff);
    for (int t = 0; t < TEAM_COUNT; t++) {
        const Player *p = &g->players[t];
        const Body *b = &p->body;
        int ax, ay, aw, ah;
        uint32_t c = t ? 0x50a0ffff : 0xff6060ff;
        outline(d, g->goal_x[t], g->goal_y[t], g->goal_w[t], g->goal_h[t], c);
        outline(d, g->home_x0[t], g->home_y0[t], g->home_x1[t] - g->home_x0[t],
                g->home_y1[t] - g->home_y0[t], c);
        outline(d, b->x, b->y, b->w, b->h, c);
        if (game_attack_box(g, p, &ax, &ay, &aw, &ah))
            outline(d, ax, ay, aw, ah, 0xffffffff);
    }
}

void scene_draw_background(const Game *g, const DrawApi *d)
{
    draw_backdrop(d);
    draw_layers(g, d);
    draw_goals(g, d);
}

void scene_draw(const Game *g, const DrawApi *d, int debug)
{
    draw_flags(g, d);
    draw_pads(g, d);
    draw_player(g, d, 1, SPR_P2);
    draw_player(g, d, 0, SPR_P1);
    draw_slash(g, d, 1);
    draw_slash(g, d, 0);
    draw_water(g, d);
    if (debug)
        draw_debug(g, d);
    if (g->title) {
        draw_title(g, d);
        return;
    }
    draw_hud(g, d);
    if (g->winner)
        draw_winner(g, d);
    else if (g->paused == PAUSE_CONTROLS)
        draw_controls(g, d);
    else if (g->paused)
        draw_pause(g, d);
    else if (g->countdown)
        draw_countdown(g, d);
}
