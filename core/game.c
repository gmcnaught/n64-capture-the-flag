#include "game.h"

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static int body_overlap(const Body *a, const Body *b)
{
    return overlap(a->x, a->y, a->w, a->h, b->x, b->y, b->w, b->h);
}

/* ---- level ---------------------------------------------------------------- */

/* Where a player stands at an entity: centered on its bottom edge. */
static int16_t stand_x(const LevelEntity *e) { return (int16_t)(e->x + (e->w - PLAYER_HIT_W) / 2); }
static int16_t stand_y(const LevelEntity *e) { return (int16_t)(e->y + e->h - PLAYER_HIT_H); }

/* A start against a wall can put the 24 px body (wider than a cell) into
 * it: move x to the nearest free spot, up to a cell either way. */
static int16_t free_x(const Level *lv, int16_t x, int16_t y)
{
    for (int d = 0; d <= TILE_W; d++) {
        if (!level_box_solid(lv, x - d, y, PLAYER_HIT_W, PLAYER_HIT_H))
            return (int16_t)(x - d);
        if (!level_box_solid(lv, x + d, y, PLAYER_HIT_W, PLAYER_HIT_H))
            return (int16_t)(x + d);
    }
    return x;
}

static void read_entities(Game *g)
{
    const Level *lv = &g->lv;
    /* Defaults without start entities: a quarter in from each side, top. */
    for (int t = 0; t < TEAM_COUNT; t++) {
        g->spawn_x[t] = (int16_t)(lv->px_w * (1 + 2 * t) / 4 - PLAYER_HIT_W / 2);
        g->spawn_y[t] = TILE_H;
        g->home_x0[t] = g->home_y0[t] = 0x7fff;
        g->home_x1[t] = g->home_y1[t] = -0x8000;
    }
    for (int i = 0; i < lv->entity_count; i++) {
        LevelEntity e;
        level_entity(lv, i, &e);
        switch (e.type) {
        case ENT_P1_START: case ENT_P2_START: {
            int t = e.type - ENT_P1_START;
            g->spawn_y[t] = stand_y(&e);
            g->spawn_x[t] = free_x(lv, stand_x(&e), g->spawn_y[t]);
            break;
        }
        case ENT_FLAG1: case ENT_FLAG2: {
            int t = e.type - ENT_FLAG1;
            if (g->flag_count == MAX_GAME_FLAGS)
                break;
            Flag *f = &g->flags[g->flag_count++];
            *f = (Flag){0};
            f->team = (uint8_t)t;
            f->home_x = (int16_t)(e.x + (e.w - FLAG_W) / 2);   /* standing on the cell's bottom */
            f->home_y = (int16_t)(e.y + e.h - FLAG_H);
            /* Home area: the flags' bounding box plus half a flag around it. */
            if (f->home_x - FLAG_W / 2 < g->home_x0[t]) g->home_x0[t] = (int16_t)(f->home_x - FLAG_W / 2);
            if (f->home_y - FLAG_H / 2 < g->home_y0[t]) g->home_y0[t] = (int16_t)(f->home_y - FLAG_H / 2);
            if (f->home_x + FLAG_W * 3 / 2 > g->home_x1[t]) g->home_x1[t] = (int16_t)(f->home_x + FLAG_W * 3 / 2);
            if (f->home_y + FLAG_H > g->home_y1[t]) g->home_y1[t] = (int16_t)(f->home_y + FLAG_H);
            break;
        }
        case ENT_GOAL1: case ENT_GOAL2: {
            int t = e.type - ENT_GOAL1;
            g->goal_x[t] = e.x;
            g->goal_y[t] = e.y;
            g->goal_w[t] = (int16_t)e.w;
            g->goal_h[t] = (int16_t)e.h;
            break;
        }
        case ENT_FRUIT_SPOT:
            if (g->fruit_spot_count < MAX_FRUIT_SPOTS) {
                g->fruit_spot_x[g->fruit_spot_count] = e.x;
                g->fruit_spot_y[g->fruit_spot_count] = e.y;
                g->fruit_spot_count++;
            }
            break;
        case ENT_JUMP_PAD:
            if (g->pad_count < MAX_PADS)
                g->pads[g->pad_count++] = (JumpPad){e.x, e.y, 0};
            break;
        }
    }
}

static void spawn_player(Game *g, int t)
{
    Player *p = &g->players[t];
    player_spawn(p, g->spawn_x[t], g->spawn_y[t], g->abilities, 0);
    /* Starts are by the middle pillar: face away from the middle, into the arena. */
    p->facing = (int8_t)(g->spawn_x[t] + PLAYER_HIT_W / 2 < g->lv.px_w / 2 ? -1 : 1);
    p->invuln = (uint8_t)g->tuning.invuln_frames;
    g->respawn[t] = 0;
    g->last_hit[1 - t] = 0;     /* this player's attack ids restart at 1 */
}

static void flag_home(Flag *f)
{
    f->state = FS_HOME;
    f->body = (Body){0};
    f->body.x = f->home_x;
    f->body.y = f->home_y;
    f->body.w = FLAG_W;
    f->body.h = FLAG_H;
}

static uint32_t rnd(Game *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}

static uint16_t fruit_interval(Game *g)
{
    uint32_t range = g->tuning.fruit_range > 0 ? (uint32_t)g->tuning.fruit_range : 1;
    uint32_t n = (uint32_t)(g->tuning.fruit_min > 1 ? g->tuning.fruit_min : 1) + rnd(g) % range;
    return (uint16_t)(n > 0xffff ? 0xffff : n);
}

static void new_round(Game *g)
{
    g->fruit_active = 0;
    g->fruit_timer = fruit_interval(g);
    g->water_len = 0;
    g->caught[0] = g->caught[1] = 0;
    for (int i = 0; i < g->flag_count; i++)
        flag_home(&g->flags[i]);
    for (int t = 0; t < TEAM_COUNT; t++) {
        g->carrying[t] = -1;
        spawn_player(g, t);
        g->players[t].invuln = 0;
    }
    g->winner = 0;
    g->round_timer = 0;
    g->countdown = COUNTDOWN_FRAMES;
    g->sfx |= SFX_BIT(SFX_MENU);    /* "3" (game_tick clears sfx, so not on game_init) */
}

/* New match: rounds won and knockouts back to 0, no menu. */
static void new_match(Game *g)
{
    for (int t = 0; t < TEAM_COUNT; t++)
        g->wins[t] = g->kos[t] = 0;
    g->title = g->paused = 0;
    g->menu_sel = MENU_RESUME;
    new_round(g);
}

void game_title(Game *g)
{
    new_match(g);
    g->title = 1;
}

void game_init(Game *g, const Level *lv, const PlayerTuning *tuning, uint8_t abilities)
{
    *g = (Game){0};
    g->lv = *lv;
    if (tuning)
        g->tuning = *tuning;
    else
        tuning_defaults(&g->tuning);
    g->abilities = abilities;
    g->rng = 0x2545f491u;
    g->stage_count = 1;
    g->backdrop = lv->backdrop < BACKDROP_COUNT ? lv->backdrop : BACKDROP_FOREST;
    read_entities(g);
    new_round(g);
}

void game_set_level(Game *g, const Level *lv)
{
    Game keep = *g;
    game_init(g, lv, &keep.tuning, keep.abilities);
    g->frame = keep.frame;
    g->title = keep.title;
    g->title_sel = keep.title_sel;
    g->stage = keep.stage;
    g->stage_count = keep.stage_count;
    for (int t = 0; t < TEAM_COUNT; t++)
        g->prev_buttons[t] = keep.prev_buttons[t];
}

/* ---- flags ------------------------------------------------------------------ */

static int alive(const Player *p)
{
    return !p->dead && p->state != PS_HURT;
}

/* Lay out the flags in team t's goal side by side on its bottom edge. */
static int layout_goal(Game *g, int t)
{
    int n = 0;
    for (int i = 0; i < g->flag_count; i++) {
        Flag *f = &g->flags[i];
        if (f->state != FS_PLACED || f->holder != t)
            continue;
        f->body.x = (int16_t)(g->goal_x[t] + g->goal_w[t] * (n + 1) / (FLAGS_PER_TEAM + 1) - FLAG_W / 2);
        f->body.y = (int16_t)(g->goal_y[t] + g->goal_h[t] - FLAG_H);
        n++;
    }
    return n;
}

/* Carrier t is hit: the flag drops where the carrier stands (then falls) and
 * the carrier goes back to its start, keeping its pips. */
static void drop_and_send_home(Game *g, int t)
{
    Player *p = &g->players[t];
    Flag *f = &g->flags[g->carrying[t]];
    f->state = FS_DROPPED;
    f->body.x = (int16_t)(p->body.x + (p->body.w - FLAG_W) / 2);
    f->body.y = (int16_t)(p->body.y + p->body.h - FLAG_H);
    f->body.vx = f->body.vy = f->body.rem_x = f->body.rem_y = 0;
    g->carrying[t] = -1;
    g->caught[t] = 0;
    player_place(p, g->spawn_x[t], g->spawn_y[t], 0);
    /* Starts are by the middle pillar: face away from the middle, into the arena. */
    p->facing = (int8_t)(g->spawn_x[t] + PLAYER_HIT_W / 2 < g->lv.px_w / 2 ? -1 : 1);
    p->invuln = (uint8_t)g->tuning.invuln_frames;
    p->attack_timer = 0;
    g->sfx |= SFX_BIT(SFX_HIT);
}

static int can_take(const Flag *f, int t)
{
    switch (f->state) {
    case FS_HOME:    return f->team != t;   /* only the other team takes it from home */
    case FS_DROPPED: return 1;
    case FS_PLACED:  return f->holder != t; /* the scoring team has no use for it */
    default:         return 0;
    }
}

static void update_flags(Game *g)
{
    for (int i = 0; i < g->flag_count; i++) {
        Flag *f = &g->flags[i];
        Body *b = &f->body;
        if (f->state == FS_CARRIED) {
            /* On the carrier's back. */
            const Player *p = &g->players[f->holder];
            b->x = (int16_t)(p->body.x + (p->body.w - FLAG_W) / 2 - p->facing * 8);
            b->y = (int16_t)(p->body.y - 6);
        } else if (f->state == FS_DROPPED) {
            b->vy += g->tuning.gravity;
            if (b->vy > g->tuning.fall_max)
                b->vy = g->tuning.fall_max;
            if (body_move_y(b, b->vy, &g->lv, 1))
                b->vy = 0;
            if (b->y >= g->lv.px_h)
                flag_home(f);               /* fell out of the arena */
        }
    }
    for (int t = 0; t < TEAM_COUNT; t++) {
        Player *p = &g->players[t];
        if (!alive(p) || g->caught[t])
            continue;
        if (g->carrying[t] < 0) {
            for (int i = 0; i < g->flag_count; i++) {
                Flag *f = &g->flags[i];
                if (can_take(f, t) && body_overlap(&p->body, &f->body)) {
                    uint8_t from_goal = f->state == FS_PLACED ? f->holder : 0xff;
                    f->state = FS_CARRIED;
                    f->holder = (uint8_t)t;
                    g->carrying[t] = (int8_t)i;
                    if (from_goal != 0xff)
                        layout_goal(g, from_goal);
                    g->sfx |= SFX_BIT(SFX_PICKUP);
                    break;
                }
            }
            continue;
        }
        Flag *f = &g->flags[g->carrying[t]];
        const Body *pb = &p->body;
        if (f->team != t && overlap(pb->x, pb->y, pb->w, pb->h, g->goal_x[t], g->goal_y[t],
                                    g->goal_w[t], g->goal_h[t])) {
            f->state = FS_PLACED;
            f->body.vy = 0;
            g->carrying[t] = -1;
            int placed = layout_goal(g, t), enemy = 0;
            for (int i = 0; i < g->flag_count; i++)
                enemy += g->flags[i].team != t;
            g->sfx |= SFX_BIT(SFX_SAVE);
            if (placed == enemy) {
                g->winner = (uint8_t)(t + 1);
                g->round_timer = ROUND_END_FRAMES;
                g->wins[t]++;
            }
        } else if (f->team == t && overlap(pb->x, pb->y, pb->w, pb->h, g->home_x0[t], g->home_y0[t],
                                           g->home_x1[t] - g->home_x0[t],
                                           g->home_y1[t] - g->home_y0[t])) {
            flag_home(f);
            g->carrying[t] = -1;
            g->sfx |= SFX_BIT(SFX_SAVE);
        }
    }
}

/* ---- player vs player bodies ----------------------------------------------- */

/* Does a stand on top of b (feet on b's head, overlapping horizontally)? */
static int stands_on(const Body *a, const Body *b)
{
    return a->y + a->h == b->y && a->x < b->x + b->w && a->x + a->w > b->x;
}

/* Collision between the players applies while both are alive and their
 * bodies do not already overlap. */
static int bodies_collide(const Game *g)
{
    const Player *a = &g->players[0], *b = &g->players[1];
    return alive(a) && alive(b) && !g->caught[0] && !g->caught[1] &&
           !body_overlap(&a->body, &b->body);
}

static void set_extra_solid(Game *g, const Body *b)
{
    g->lv.solid_x = b->x;
    g->lv.solid_y = b->y;
    g->lv.solid_w = b->w;
    g->lv.solid_h = b->h;
}

static void clear_extra_solid(Game *g)
{
    g->lv.solid_w = 0;
}

/* After the player underneath moved, a rider it moved into is lifted onto
 * its head; if the rider is blocked (ceiling), the lower one stops under it. */
static void lift_rider(Game *g, Player *under, Player *rider)
{
    Body *u = &under->body, *r = &rider->body;
    if (!body_overlap(u, r))
        return;
    clear_extra_solid(g);
    body_move_y(r, -FX(r->y + r->h - u->y), &g->lv, 0);
    if (r->y + r->h > u->y) {
        u->y = (int16_t)(r->y + r->h);
        u->rem_y = 0;
        if (u->vy < 0)
            u->vy = 0;
    }
}

/* ---- water fruit (Bubble Bobble water) ------------------------------------ */

static int water_floor(const Level *lv, int cx, int cy)
{
    uint8_t c = level_cell(lv, cx, cy);
    return c == CELL_SOLID || c == CELL_ONEWAY;
}

/* Bottom ground: solid from below the cell down to the arena's bottom. */
static int water_on_ground(const Level *lv, int cx, int cy)
{
    if (cy + 1 >= lv->cell_h)
        return 0;
    for (int y = cy + 1; y < lv->cell_h; y++)
        if (level_cell(lv, cx, y) != CELL_SOLID)
            return 0;
    return 1;
}

/* Row of the first floor below (cx, cy), or the arena height if none. */
static int water_drop_row(const Level *lv, int cx, int cy)
{
    for (int y = cy + 1; y < lv->cell_h; y++)
        if (water_floor(lv, cx, y))
            return y;
    return lv->cell_h;
}

/* The front just ran off an edge: how many cells to run on so the fall
 * lands on the highest floor within WATER_OVERHANG cells (0: fall here). */
static int water_overhang(const Game *g, int cx, int cy)
{
    const Level *lv = &g->lv;
    int best = water_drop_row(lv, cx, cy), extend = 0;
    for (int k = 1; k <= WATER_OVERHANG; k++) {
        int x = cx + k * g->water_dir;
        if (level_cell(lv, x, cy) == CELL_SOLID)
            break;
        int row = water_drop_row(lv, x, cy);
        if (row < best) {
            best = row;
            extend = k;
        }
    }
    return extend;
}

/* Next cell of the front, or 0 if the stream drains here. */
static int water_next(Game *g, int *nx, int *ny)
{
    const Level *lv = &g->lv;
    int cx = g->water_cx[0], cy = g->water_cy[0];
    if (g->water_steps >= WATER_MAX_STEPS || cy >= lv->cell_h - 1)
        return 0;
    if (!water_floor(lv, cx, cy + 1)) {
        if (!g->water_air) {        /* just left a floor */
            g->water_air = 1;
            g->water_extend = (uint8_t)water_overhang(g, cx, cy);
        }
        if (g->water_extend && level_cell(lv, cx + g->water_dir, cy) != CELL_SOLID) {
            g->water_extend--;
            *nx = cx + g->water_dir;
            *ny = cy;
            return 1;
        }
        g->water_extend = 0;
        *nx = cx;
        *ny = cy + 1;
        return 1;
    }
    g->water_air = 0;
    for (int turn = 0; turn < 2; turn++) {
        if (level_cell(lv, cx + g->water_dir, cy) != CELL_SOLID) {
            *nx = cx + g->water_dir;
            *ny = cy;
            return 1;
        }
        if (water_on_ground(lv, cx, cy))
            return 0;                   /* reached a wall on the bottom ground */
        g->water_dir = (int8_t)-g->water_dir;
    }
    return 0;                           /* boxed in */
}

/* A caught player stands at the front of the stream. */
static void water_hold(Game *g, int t)
{
    Body *b = &g->players[t].body;
    b->x = (int16_t)(g->water_cx[0] * TILE_W + (TILE_W - b->w) / 2);
    b->y = (int16_t)(g->water_cy[0] * TILE_H + TILE_H - b->h);
    b->vx = b->vy = b->rem_x = b->rem_y = 0;
}

/* Put a caught player down at the front, nudged sideways out of walls. */
static void water_release(Game *g, int t)
{
    Player *p = &g->players[t];
    Body *b = &p->body;
    water_hold(g, t);
    static const int8_t nudge[] = {0, 4, -4, 8, -8, 12, -12};
    for (unsigned i = 0; i < sizeof nudge; i++)
        if (!level_box_solid(&g->lv, b->x + nudge[i], b->y, b->w, b->h)) {
            b->x = (int16_t)(b->x + nudge[i]);
            break;
        }
    g->caught[t] = 0;
    p->invuln = 30;
}

static void update_water(Game *g)
{
    /* Fruit: appear, wait to be touched, disappear. */
    if (g->water_len == 0 && g->fruit_spot_count) {
        if (g->fruit_timer && --g->fruit_timer == 0) {
            if (g->fruit_active) {
                g->fruit_active = 0;
                g->fruit_timer = fruit_interval(g);
            } else {
                g->fruit_active = 1;
                g->fruit_spot = (uint8_t)(rnd(g) % g->fruit_spot_count);
                g->fruit_timer = FRUIT_LIFE;
            }
        }
    }
    if (g->fruit_active) {
        int fx = g->fruit_spot_x[g->fruit_spot], fy = g->fruit_spot_y[g->fruit_spot];
        for (int t = 0; t < TEAM_COUNT; t++) {
            const Player *p = &g->players[t];
            if (!alive(p) || g->caught[t] ||
                !overlap(p->body.x, p->body.y, p->body.w, p->body.h, fx, fy, TILE_W, TILE_H))
                continue;
            g->fruit_active = 0;
            g->fruit_timer = fruit_interval(g);
            g->water_len = 1;
            g->water_cx[0] = (int16_t)(fx >> TILE_SHIFT_X);
            g->water_cy[0] = (int16_t)(fy >> TILE_SHIFT_Y);
            g->water_dir = p->facing;
            g->water_draining = 0;
            g->water_air = 0;
            g->water_extend = 0;
            g->water_steps = 0;
            g->water_step_timer = WATER_STEP;
            g->water_owner = (uint8_t)t;
            g->water_grace = WATER_GRACE;
            g->sfx |= SFX_BIT(SFX_PICKUP);
            break;
        }
    }
    if (g->water_len == 0)
        return;

    /* Stream: the front moves a cell every WATER_STEP frames; draining, the
     * tail catches up with the front. */
    if (g->water_grace)
        g->water_grace--;
    if (--g->water_step_timer == 0) {
        g->water_step_timer = WATER_STEP;
        int nx = 0, ny = 0;
        if (!g->water_draining && !water_next(g, &nx, &ny)) {
            g->water_draining = 1;
            for (int t = 0; t < TEAM_COUNT; t++)
                if (g->caught[t])
                    water_release(g, t);
        }
        if (g->water_draining) {
            if (--g->water_len == 0)
                return;
        } else {
            int n = g->water_len < WATER_LEN ? g->water_len + 1 : WATER_LEN;
            for (int i = n - 1; i > 0; i--) {
                g->water_cx[i] = g->water_cx[i - 1];
                g->water_cy[i] = g->water_cy[i - 1];
            }
            g->water_cx[0] = (int16_t)nx;
            g->water_cy[0] = (int16_t)ny;
            g->water_len = (uint8_t)n;
            g->water_steps++;
        }
    }
    for (int t = 0; t < TEAM_COUNT; t++) {
        Player *p = &g->players[t];
        if (g->caught[t]) {
            water_hold(g, t);
            continue;
        }
        if (g->water_draining || !alive(p) || (t == g->water_owner && g->water_grace))
            continue;
        for (int i = 0; i < g->water_len; i++)
            if (overlap(p->body.x, p->body.y, p->body.w, p->body.h,
                        g->water_cx[i] * TILE_W, g->water_cy[i] * TILE_H, TILE_W, TILE_H)) {
                g->caught[t] = 1;
                p->state = PS_NORMAL;
                p->attack_timer = p->dash_timer = p->knock = 0;
                water_hold(g, t);
                g->sfx |= SFX_BIT(SFX_PAD);
                break;
            }
    }
}

/* ---- combat ----------------------------------------------------------------- */

int game_attack_box(const Game *g, const Player *p, int *x, int *y, int *w, int *h)
{
    return player_attack_box(p, &g->tuning, x, y, w, h);
}

/* Player t's attack against the opponent: each swing hits once. A down-hit
 * bounces the attacker (pogo). A carrier drops the flag and goes home. */
static void resolve_attack(Game *g, int t)
{
    Player *p = &g->players[t], *o = &g->players[1 - t];
    int ax, ay, aw, ah;
    if (!alive(p) || !player_attack_box(p, &g->tuning, &ax, &ay, &aw, &ah))
        return;
    const Body *pb = &p->body, *ob = &o->body;
    if (!alive(o) || o->invuln || g->caught[1 - t] || g->last_hit[1 - t] == p->attack_id ||
        !overlap(ax, ay, aw, ah, ob->x, ob->y, ob->w, ob->h))
        return;
    g->last_hit[1 - t] = p->attack_id;
    int dir = p->attack_dir == ATK_SIDE ? p->facing
            : (ob->x + ob->w / 2 >= pb->x + pb->w / 2 ? 1 : -1);
    if (g->carrying[1 - t] >= 0) {
        drop_and_send_home(g, 1 - t);
    } else if (player_damage(o, &g->tuning, dir)) {
        g->sfx |= SFX_BIT(o->health ? SFX_HIT : SFX_KILL);
        if (o->health == 0)
            g->kos[t]++;
    }
    if (p->attack_dir == ATK_DOWN && !p->attack_pogo)
        player_pogo(p, &g->tuning);
}

static void use_pads(Game *g, Player *p)
{
    const Body *b = &p->body;
    for (int i = 0; i < g->pad_count; i++) {
        JumpPad *pad = &g->pads[i];
        if (p->state != PS_CLIMB && p->state != PS_HURT && b->vy >= 0 &&
            overlap(b->x, b->y, b->w, b->h, pad->x + PAD_HIT_X, pad->y + PAD_HIT_Y,
                    TILE_W - 2 * PAD_HIT_X, TILE_H - PAD_HIT_Y)) {
            player_launch(p, g->tuning.pad_speed);
            pad->timer = PAD_ANIM_FRAMES;
        }
    }
}

/* ---- screens ---------------------------------------------------------------- */

/* Pause menu, driven by either player's presses. */
static void pause_menu(Game *g, uint16_t pressed)
{
    if (pressed & (BTN_UP | BTN_DOWN)) {
        int step = (pressed & BTN_UP) ? MENU_COUNT - 1 : 1;
        g->menu_sel = (uint8_t)((g->menu_sel + step) % MENU_COUNT);
        g->sfx |= SFX_BIT(SFX_MENU);
    }
    if (!(pressed & (BTN_JUMP | BTN_ATTACK)))
        return;
    g->sfx |= SFX_BIT(SFX_MENU);
    switch (g->menu_sel) {
    case MENU_RESUME:   g->paused = 0; break;
    case MENU_CONTROLS: g->paused = PAUSE_CONTROLS; break;
    case MENU_RESTART:  new_match(g); break;
    case MENU_QUIT:     game_title(g); break;
    }
}

/* Title screen rows: Up/Down pick one, Left/Right change it (wrapping). */
static void title_menu(Game *g, uint16_t pressed)
{
    if (pressed & (BTN_UP | BTN_DOWN)) {
        int step = (pressed & BTN_UP) ? TITLE_ROWS - 1 : 1;
        g->title_sel = (uint8_t)((g->title_sel + step) % TITLE_ROWS);
        g->sfx |= SFX_BIT(SFX_MENU);
    }
    int step = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : 0;
    if (!step)
        return;
    if (g->title_sel == TITLE_STAGE) {
        if (g->stage_count < 2)
            return;
        g->stage = (uint8_t)((g->stage + step + g->stage_count) % g->stage_count);
    } else {
        g->backdrop = (uint8_t)((g->backdrop + step + BACKDROP_COUNT) % BACKDROP_COUNT);
    }
    g->sfx |= SFX_BIT(SFX_MENU);
}

/* ---- tick ------------------------------------------------------------------ */

void game_tick(Game *g, const InputState in[TEAM_COUNT])
{
    uint16_t pressed[TEAM_COUNT];
    for (int t = 0; t < TEAM_COUNT; t++) {
        pressed[t] = in[t].buttons & (uint16_t)~g->prev_buttons[t];
        g->prev_buttons[t] = in[t].buttons;
    }
    g->sfx = 0;
    g->frame++;

    if (g->title) {                 /* title screen: the sim waits */
        if ((pressed[0] | pressed[1]) & (BTN_START | BTN_JUMP)) {
            new_match(g);
            g->sfx |= SFX_BIT(SFX_MENU);
        } else {
            title_menu(g, pressed[0] | pressed[1]);
        }
        return;
    }
    if (g->winner) {                /* round-end banner: the sim waits */
        if (--g->round_timer == 0)
            new_round(g);
        return;
    }
    if (g->paused == PAUSE_CONTROLS) {  /* controls screen: back to the menu */
        if ((pressed[0] | pressed[1]) & (BTN_START | BTN_JUMP | BTN_ATTACK)) {
            g->paused = PAUSE_MENU;
            g->sfx |= SFX_BIT(SFX_MENU);
        }
        return;
    }
    if ((pressed[0] | pressed[1]) & BTN_START) {
        g->paused = g->paused ? 0 : PAUSE_MENU;
        g->menu_sel = MENU_RESUME;
        g->sfx |= SFX_BIT(SFX_MENU);
        return;
    }
    if (g->paused) {
        pause_menu(g, pressed[0] | pressed[1]);
        return;
    }
    if (g->countdown > COUNTDOWN_GO) {  /* round countdown: the sim waits */
        g->countdown--;
        if (g->countdown == COUNTDOWN_GO)
            g->sfx |= SFX_BIT(SFX_PICKUP);                      /* GO! */
        else if ((g->countdown - COUNTDOWN_GO) % COUNTDOWN_STEP == 0)
            g->sfx |= SFX_BIT(SFX_MENU);                        /* 2, 1 */
        return;
    }
    if (g->countdown)
        g->countdown--;

    uint8_t stacked_before[TEAM_COUNT];     /* the other player stood on t */
    for (int t = 0; t < TEAM_COUNT; t++)
        stacked_before[t] = bodies_collide(g) &&
                            stands_on(&g->players[1 - t].body, &g->players[t].body);
    for (int t = 0; t < TEAM_COUNT; t++) {
        Player *p = &g->players[t], *o = &g->players[1 - t];
        if (p->dead) {
            if (++g->respawn[t] >= RESPAWN_FRAMES)
                spawn_player(g, t);
            continue;
        }
        if (g->caught[t])           /* carried by the water: no control */
            continue;
        InputState pin = in[t];
        uint16_t ppressed = pressed[t];
        PlayerTuning mod = g->tuning;
        if (g->carrying[t] >= 0) {  /* carriers run slower and cannot dash or attack */
            pin.buttons &= (uint16_t)~(BTN_DASH | BTN_ATTACK);
            ppressed &= (uint16_t)~(BTN_DASH | BTN_ATTACK);
            mod.run_speed = mod.carry_speed;
        }
        /* The other player is a solid, except when it rides on t (t moves
         * freely and lifts it afterwards). */
        int collide = bodies_collide(g);
        int ridden = collide && stands_on(&o->body, &p->body);
        if (ridden) {               /* weighed down */
            if (mod.weight_speed < mod.run_speed)
                mod.run_speed = mod.weight_speed;
            mod.jump_speed = mod.double_jump_speed = mod.weight_jump;
        }
        if (collide && !ridden)
            set_extra_solid(g, &o->body);
        player_tick(p, &g->lv, &mod, &pin, ppressed);
        clear_extra_solid(g);
        if (ridden)
            lift_rider(g, p, o);
    }
    /* A rider landing on a rising player stops that jump. */
    for (int t = 0; t < TEAM_COUNT; t++) {
        Player *p = &g->players[t];
        if (!stacked_before[t] && bodies_collide(g) &&
            stands_on(&g->players[1 - t].body, &p->body) && p->body.vy < 0) {
            p->body.vy = 0;
            p->body.rem_y = 0;
            p->jumping = 0;
        }
    }
    for (int t = 0; t < TEAM_COUNT; t++)
        resolve_attack(g, t);
    for (int t = 0; t < TEAM_COUNT; t++) {
        Player *p = &g->players[t];
        Body *b = &p->body;
        if (!alive(p) || g->caught[t])
            continue;
        if (b->y >= g->lv.px_h || level_box_hazard(&g->lv, b->x, b->y, b->w, b->h)) {
            if (g->carrying[t] >= 0)
                drop_and_send_home(g, t);
            else
                player_hurt(p, &g->tuning);
        } else {
            use_pads(g, p);
        }
    }
    update_water(g);
    update_flags(g);
    for (int i = 0; i < g->pad_count; i++)
        if (g->pads[i].timer)
            g->pads[i].timer--;
    for (int t = 0; t < TEAM_COUNT; t++)
        g->sfx |= g->players[t].sfx;
}

/* ---- hash ------------------------------------------------------------------ */

static uint32_t mix(uint32_t h, uint32_t v)
{
    h = (h << 5) | (h >> 27);
    return (h ^ v) * 0x01000193u;
}

static uint32_t hash_body(uint32_t h, const Body *b)
{
    h = mix(h, (uint16_t)b->x | (uint32_t)(uint16_t)b->y << 16);
    h = mix(h, (uint16_t)b->w | (uint32_t)(uint16_t)b->h << 16);
    h = mix(h, (uint32_t)b->rem_x);
    h = mix(h, (uint32_t)b->rem_y);
    h = mix(h, (uint32_t)b->vx);
    return mix(h, (uint32_t)b->vy);
}

static uint32_t hash_player(uint32_t h, const Player *p)
{
    h = hash_body(h, &p->body);
    h = mix(h, p->state | (uint32_t)(uint8_t)p->facing << 8 | (uint32_t)p->health << 16 |
               (uint32_t)p->abilities << 24);
    h = mix(h, p->air_jumps | (uint32_t)p->air_dashes << 8 | (uint32_t)p->coyote << 16 |
               (uint32_t)p->jump_buffer << 24);
    h = mix(h, p->jumping | (uint32_t)p->wall_lock << 8 | (uint32_t)p->drop << 16 |
               (uint32_t)p->ladder_lock << 24);
    h = mix(h, (uint32_t)(uint8_t)p->dash_dir | (uint32_t)p->dash_timer << 8 |
               (uint32_t)p->dash_cooldown << 16 | (uint32_t)p->hurt_timer << 24);
    h = mix(h, p->invuln | (uint32_t)p->land_timer << 8 | (uint32_t)p->grounded << 16 |
               (uint32_t)(uint8_t)p->wall_dir << 24);
    h = mix(h, p->attack_timer | (uint32_t)p->attack_dir << 8 | (uint32_t)p->attack_cooldown << 16 |
               (uint32_t)p->attack_id << 24);
    h = mix(h, p->attack_pogo | (uint32_t)p->knock << 8 | (uint32_t)p->modifiers << 16 |
               (uint32_t)p->shield << 24);
    h = mix(h, p->shield_timer | (uint32_t)p->dead << 16 | (uint32_t)p->wall_sliding << 24);
    h = mix(h, (uint32_t)p->push_x);
    return mix(h, (uint16_t)p->safe_x | (uint32_t)(uint16_t)p->safe_y << 16);
}

uint32_t game_hash(const Game *g)
{
    uint32_t h = 0x811c9dc5u;
    h = mix(h, g->frame);
    h = mix(h, g->paused | (uint32_t)g->abilities << 8 | (uint32_t)g->title << 16 |
               (uint32_t)g->menu_sel << 24);
    if (g->title_sel | g->stage | g->backdrop)  /* earlier replays keep their hashes */
        h = mix(h, g->title_sel | (uint32_t)g->stage << 8 | (uint32_t)g->backdrop << 16);
    for (int t = 0; t < TEAM_COUNT; t++) {
        h = hash_player(h, &g->players[t]);
        h = mix(h, g->last_hit[t] | (uint32_t)g->respawn[t] << 8);
        h = mix(h, g->kos[t] | (uint32_t)g->prev_buttons[t] << 16);
        h = mix(h, (uint16_t)g->spawn_x[t] | (uint32_t)(uint16_t)g->spawn_y[t] << 16);
    }
    for (int i = 0; i < g->pad_count; i++)
        h = mix(h, g->pads[i].timer);
    for (int i = 0; i < g->flag_count; i++) {
        const Flag *f = &g->flags[i];
        h = hash_body(h, &f->body);
        h = mix(h, f->team | (uint32_t)f->state << 8 | (uint32_t)f->holder << 16);
    }
    for (int t = 0; t < TEAM_COUNT; t++)
        h = mix(h, (uint32_t)(uint8_t)g->carrying[t] | (uint32_t)g->wins[t] << 16);
    h = mix(h, g->winner | (uint32_t)g->round_timer << 8 | (uint32_t)g->countdown << 24);
    h = mix(h, g->rng);
    h = mix(h, g->fruit_active | (uint32_t)g->fruit_spot << 8 | (uint32_t)g->fruit_timer << 16);
    h = mix(h, g->water_len | (uint32_t)(uint8_t)g->water_dir << 8 |
               (uint32_t)g->water_draining << 16 | (uint32_t)g->water_step_timer << 24);
    h = mix(h, g->water_steps | (uint32_t)g->water_owner << 16 | (uint32_t)g->water_grace << 24);
    h = mix(h, g->water_air | (uint32_t)g->water_extend << 8);
    for (int i = 0; i < g->water_len; i++)
        h = mix(h, (uint16_t)g->water_cx[i] | (uint32_t)(uint16_t)g->water_cy[i] << 16);
    h = mix(h, g->caught[0] | (uint32_t)g->caught[1] << 8);
    for (int i = 0; i < tuning_field_count; i++)
        h = mix(h, (uint32_t)*(const int32_t *)((const char *)&g->tuning + tuning_fields[i].offset));
    return h;
}
