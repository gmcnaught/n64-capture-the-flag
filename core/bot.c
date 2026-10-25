#include "bot.h"

#define MOVE_MAX_FRAMES 200
#define SNAP_X 12                   /* px a standing body may be off its node */
#define LAND_X 28                   /* px a landing may be off a node, walking on */
#define PLAT_REACH 224              /* px from a platform end to the nodes that try moves onto it */

static int iabs(int v) { return v < 0 ? -v : v; }

/* ---- navigation graph -------------------------------------------------------- */

static bool body_fits(const Level *lv, int x, int y)
{
    return !level_box_solid(lv, x, y, PLAYER_HIT_W, PLAYER_HIT_H) &&
           !level_box_hazard(lv, x - 2, y, PLAYER_HIT_W + 4, PLAYER_HIT_H + 1);
}

/* The level with platform p standing at end k as its one solid (p -1: none). */
static void level_with(const BotNav *nav, Level *out, int p, int k)
{
    *out = nav->lv;
    out->solid_count = 0;
    if (p >= 0) {
        out->solids[0] = (Solid){nav->plat_x[p][k], nav->plat_y[p][k], nav->plat_w[p],
                                 nav->plat_h[p], 1, nav->plat_oneway[p]};
        out->solid_count = 1;
    }
}

static void add_node(BotNav *nav, int x, int y, int p, int k)
{
    if (nav->node_count >= NAV_MAX_NODES - 1)
        return;
    int i = nav->node_count++;
    nav->node_x[i] = (int16_t)x;
    nav->node_y[i] = (int16_t)y;
    nav->node_plat[i] = (int8_t)p;
    nav->node_end[i] = (uint8_t)k;
}

/* Standing spots: per floor cell, the body centered on it (or nudged half
 * a cell) with its feet on the cell's top; then the middle of each moving
 * platform's top at each end. */
static void find_nodes(BotNav *nav)
{
    const Level *lv = &nav->lv;
    nav->node_count = 0;
    for (int cy = 2; cy < lv->cell_h; cy++)
        for (int cx = 0; cx < lv->cell_w; cx++) {
            uint8_t c = level_cell(lv, cx, cy);
            if (c != CELL_SOLID && !level_cell_platform(lv, cx, cy))
                continue;
            if (level_cell(lv, cx, cy - 1) == CELL_SOLID)
                continue;
            int y = cy * TILE_H - PLAYER_HIT_H;
            static const int8_t nudge[] = {0, 4, -4, 8, -8};
            for (unsigned k = 0; k < sizeof nudge; k++) {
                int x = cx * TILE_W + (TILE_W - PLAYER_HIT_W) / 2 + nudge[k];
                if (body_fits(lv, x, y)) {
                    add_node(nav, x, y, -1, 0);
                    break;
                }
            }
        }
    for (int p = 0; p < nav->plat_count; p++)
        for (int k = 0; k < 2; k++) {
            int x = nav->plat_x[p][k] + (nav->plat_w[p] - PLAYER_HIT_W) / 2;
            int y = nav->plat_y[p][k] - PLAYER_HIT_H;
            if (body_fits(lv, x, y))
                add_node(nav, x, y, p, k);
        }
}

static int plat_node(const BotNav *nav, int p, int k)
{
    for (int i = 0; i < nav->node_count; i++)
        if (nav->node_plat[i] == p && nav->node_end[i] == k)
            return i;
    return -1;
}

/* Static node at a standing position: same y, within snap px; -1 if none. */
static int node_near(const BotNav *nav, int x, int y, int snap)
{
    int best = -1, bd = snap + 1;
    for (int i = 0; i < nav->node_count; i++) {
        int d = iabs(nav->node_x[i] - x);
        if (nav->node_plat[i] < 0 && nav->node_y[i] == y && d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* Can a body walk along row y from x0 to x1 (standing all the way)? */
static bool floor_between(const Level *lv, int x0, int x1, int y)
{
    for (int x = x0; x != x1; x += x1 > x0 ? 1 : -1)
        if (!body_fits(lv, x, y) || (!level_box_solid(lv, x, y + 1, PLAYER_HIT_W, PLAYER_HIT_H) &&
                                    !level_box_has(lv, x, y + PLAYER_HIT_H, PLAYER_HIT_W, 1, CELL_ONEWAY)))
            return false;
    return true;
}

uint16_t bot_move_buttons(const NavEdge *e, int f)
{
    uint16_t d = e->dir < 0 ? BTN_LEFT : e->dir > 0 ? BTN_RIGHT : 0;
    uint16_t a = e->air < 0 ? BTN_LEFT : e->air > 0 ? BTN_RIGHT : 0;
    if (f >= MOVE_MAX_FRAMES)
        return 0xffff;
    switch (e->kind) {
    case NAV_FALL:
        return f < e->run ? d : a;
    case NAV_DROP:
        return f < 2 ? BTN_DOWN : f < 4 ? BTN_DOWN | BTN_JUMP : a;
    case NAV_JUMP: {
        int delay = e->dir ? 0 : e->run;    /* straight up: run is the air delay */
        if (e->dir && f < e->run)
            return d;
        int j = e->dir ? f - e->run : f;
        if (j == 0)
            return d | BTN_JUMP;
        uint16_t b = j >= delay ? a : 0;
        if (e->dbl ? (j < e->dbl - 1 || j >= e->dbl) : j < e->hold)
            b |= BTN_JUMP;
        return b;
    }
    default:
        return d;
    }
}

/* Play move e from rest at node from in level lv (the static level, or with
 * one platform at an end), then no input from the landing until at rest: the
 * node it rests at (e->to), the frames it took
 * (e->cost) and the goal zones touched on the way. With onto >= 0 (a
 * platform node), only a landing on that platform counts. A move that
 * touches a goal counts even if it lands off the graph or back where it
 * started (e->to is then from). false if it fails. */
static bool sim_move(const BotNav *nav, const Level *lv, const PlayerTuning *t, int from, int onto,
                    NavEdge *e)
{
    Player p;
    player_spawn(&p, nav->node_x[from], nav->node_y[from], ABIL_ALL, 0);
    InputState in = {BTN_CONNECTED, 0, 0};
    uint16_t prev = 0;
    player_tick(&p, lv, t, &in, 0);
    if (!p.grounded)
        return false;
    bool air = false, landed = false;
    int inside = 0;                     /* goal zones it starts in */
    e->touch = e->touch_cost = 0;
    for (int k = 0; k < TEAM_COUNT; k++)
        if (p.body.x < nav->goal_x[k] + nav->goal_w[k] && p.body.x + p.body.w > nav->goal_x[k] &&
            p.body.y < nav->goal_y[k] + nav->goal_h[k] && p.body.y + p.body.h > nav->goal_y[k])
            inside |= 1 << k;
    for (int f = 0; f < MOVE_MAX_FRAMES; f++) {
        uint16_t h = landed ? 0 : bot_move_buttons(e, f);
        in.buttons = (uint16_t)(h | BTN_CONNECTED);
        player_tick(&p, lv, t, &in, (uint16_t)(h & ~prev));
        prev = h;
        const Body *b = &p.body;
        if (b->y >= lv->px_h || p.state == PS_CLIMB ||
            level_box_hazard(lv, b->x, b->y, b->w, b->h))
            return false;
        for (int k = 0; k < TEAM_COUNT && onto < 0; k++)
            if (!e->touch && !(inside & (1 << k)) && b->x < nav->goal_x[k] + nav->goal_w[k] && b->x + b->w > nav->goal_x[k] &&
                b->y < nav->goal_y[k] + nav->goal_h[k] && b->y + b->h > nav->goal_y[k]) {
                e->touch = (uint8_t)(1u << k);
                e->touch_cost = (uint8_t)(f + 1);
            }
        if (!p.grounded) {
            air = true;
            continue;
        }
        if (!air)
            continue;
        landed = true;                  /* then no input until at rest (ice slides on) */
        if (b->vx != 0)
            continue;
        int n = -1;
        const Solid *s = &lv->solids[0];
        bool on_plat = lv->solid_count && b->y + b->h == s->y && b->x < s->x + s->w &&
                       b->x + b->w > s->x;
        if (onto >= 0) {
            if (!on_plat)
                return false;
            n = onto;
        } else if (on_plat) {
            n = -1;                     /* back on the platform it left */
        } else {
            n = node_near(nav, b->x, b->y, LAND_X);
            if (n >= 0 && !floor_between(&nav->lv, b->x, nav->node_x[n], b->y))
                n = -1;
        }
        if ((n < 0 || n == from) && !e->touch)
            return false;
        e->to = (uint16_t)(n < 0 ? from : n);
        e->cost = (uint16_t)(f + 1 + (n < 0 || onto >= 0 ? 0 : FX(iabs(nav->node_x[n] - b->x)) / t->run_speed));
        return true;
    }
    return false;
}

/* Keep the quickest move to each node, and through each goal: edges of the
 * node being built are [start, edge_count). */
static void add_edge(NavGraph *gr, int start, const NavEdge *e)
{
    for (int i = start; i < gr->edge_count; i++) {
        NavEdge *o = &gr->edges[i];
        if (o->touch == e->touch && (e->touch || o->to == e->to)) {
            if (e->touch ? e->touch_cost < o->touch_cost : e->cost < o->cost)
                *o = *e;
            return;
        }
    }
    if (gr->edge_count < NAV_MAX_EDGES)
        gr->edges[gr->edge_count++] = *e;
}

/* Frames platform p takes to go to one end and back, waits included. */
static int plat_cycle(const BotNav *nav, int p)
{
    int len = iabs(nav->plat_x[p][1] - nav->plat_x[p][0]);
    if (iabs(nav->plat_y[p][1] - nav->plat_y[p][0]) > len)
        len = iabs(nav->plat_y[p][1] - nav->plat_y[p][0]);
    return nav->plat_speed[p] > 0 ? 2 * (FX(len) / nav->plat_speed[p] + PLATFORM_WAIT) : 0;
}

static void try_move(const BotNav *nav, NavGraph *gr, const Level *lv, const PlayerTuning *t,
                     int from, int onto, int start, NavEdge e)
{
    if (sim_move(nav, lv, t, from, onto, &e)) {
        if (onto >= 0)                  /* on average, half a cycle waiting for the platform */
            e.cost = (uint16_t)(e.cost + plat_cycle(nav, nav->node_plat[onto]) / 2);
        add_edge(gr, start, &e);
        if (e.touch && e.to != from) {  /* also a move to where it lands */
            e.touch = e.touch_cost = 0;
            add_edge(gr, start, &e);
        }
    }
}

/* The move set from node n in level lv. */
static void try_moves(const BotNav *nav, NavGraph *gr, const Level *lv, const PlayerTuning *t,
                      int n, int onto, int start)
{
    static const uint8_t runs[] = {0, 3, 6, 9, 12, 15};
    static const uint8_t dbls[] = {0, 8, 12, 16, 20, 24, 28, 32};
    static const uint8_t delays[] = {0, 8, 16, 24};        /* straight up: air after */
    static const uint8_t fall_runs[] = {8, 14};
    for (int dir = -1; dir <= 1; dir++) {
        for (unsigned r = 0; r < (dir ? sizeof runs : sizeof delays); r++)
            for (int air = -1; air <= 1; air++) {
                if (dir && air && air != dir)
                    continue;           /* running one way, steering the other: left out */
                if (!dir && !air && r)
                    continue;
                NavEdge e = {0, 0, NAV_JUMP, (int8_t)dir, (int8_t)air, dir ? runs[r] : delays[r],
                             5, 0, 0, 0};
                try_move(nav, gr, lv, t, n, onto, start, e);            /* short hop */
                e.hold = 0xff;
                for (unsigned d = 0; d < sizeof dbls; d++) {
                    e.dbl = dbls[d];
                    try_move(nav, gr, lv, t, n, onto, start, e);
                }
            }
        if (!dir)
            continue;
        for (unsigned r = 0; r < sizeof fall_runs; r++)
            for (int air = 0; air <= 1; air++) {
                NavEdge e = {0, 0, NAV_FALL, (int8_t)dir, (int8_t)(air ? dir : 0), fall_runs[r],
                             0, 0, 0, 0};
                try_move(nav, gr, lv, t, n, onto, start, e);
            }
    }
    int p = nav->node_plat[n];
    int cy = (nav->node_y[n] + PLAYER_HIT_H) >> TILE_SHIFT_Y;
    int cx = (nav->node_x[n] + PLAYER_HIT_W / 2) >> TILE_SHIFT_X;
    if (p >= 0 ? nav->plat_oneway[p] : level_cell_platform(lv, cx, cy))
        for (int air = -1; air <= 1; air++) {
            NavEdge e = {0, 0, NAV_DROP, 0, (int8_t)air, 0, 0, 0, 0, 0};
            try_move(nav, gr, lv, t, n, onto, start, e);
        }
}

static void build_node(BotNav *nav, int gi, int n)
{
    NavGraph *gr = &nav->graphs[gi];
    PlayerTuning t = nav->tuning;
    if (gi == 1)
        t.run_speed = t.carry_speed;
    int start = gr->edge_count, p = nav->node_plat[n];
    gr->first[n] = (uint16_t)start;
    Level lv;
    level_with(nav, &lv, p, nav->node_end[n]);
    /* Walking to the neighbor nodes on the row (16 px a cell). */
    for (int k = 0; k < nav->node_count && p < 0; k++) {
        int dx = nav->node_x[k] - nav->node_x[n];
        if (k == n || nav->node_plat[k] >= 0 || nav->node_y[k] != nav->node_y[n] ||
            iabs(dx) > TILE_W + 8 || !floor_between(&lv, nav->node_x[n], nav->node_x[k], nav->node_y[n]))
            continue;
        NavEdge e = {(uint16_t)k, (uint16_t)(FX(iabs(dx)) / t.run_speed + 1), NAV_WALK,
                     (int8_t)(dx > 0 ? 1 : -1), 0, 0, 0, 0, 0, 0};
        add_edge(gr, start, &e);
    }
    try_moves(nav, gr, &lv, &t, n, -1, start);
    if (p < 0) {
        /* Onto the platform ends in reach (the platform there, standing still). */
        for (int k = 0; k < nav->node_count; k++) {
            int q = nav->node_plat[k];
            if (q < 0 || iabs(nav->node_x[k] - nav->node_x[n]) > PLAT_REACH ||
                nav->node_y[k] - nav->node_y[n] > PLAT_REACH || nav->node_y[n] - nav->node_y[k] > 96)
                continue;
            Level lp;
            level_with(nav, &lp, q, nav->node_end[k]);
            if (body_fits(&lp, nav->node_x[n], nav->node_y[n]))
                try_moves(nav, gr, &lp, &t, n, k, start);
        }
    } else {
        /* Riding to the other end: half a cycle. */
        int o = plat_node(nav, p, 1 - nav->node_end[n]);
        if (o >= 0 && plat_cycle(nav, p)) {
            NavEdge e = {(uint16_t)o, (uint16_t)(plat_cycle(nav, p) / 2), NAV_RIDE, 0, 0, 0, 0, 0,
                         0, 0};
            add_edge(gr, start, &e);
        }
    }
    gr->first[n + 1] = gr->edge_count;
}

void bot_nav_begin(BotNav *nav, const Level *lv, const PlayerTuning *t)
{
    nav->lv = *lv;
    nav->lv.solid_count = 0;
    nav->tuning = *t;
    nav->graph = 0;
    nav->next = 0;
    nav->done = false;
    nav->plat_count = 0;
    for (int k = 0; k < TEAM_COUNT; k++)
        nav->goal_x[k] = nav->goal_y[k] = nav->goal_w[k] = nav->goal_h[k] = 0;
    for (int i = 0; i < lv->entity_count; i++) {
        LevelEntity e;
        level_entity(lv, i, &e);
        if (e.type == ENT_GOAL1 || e.type == ENT_GOAL2) {
            int k = e.type - ENT_GOAL1;
            nav->goal_x[k] = e.x;
            nav->goal_y[k] = e.y;
            nav->goal_w[k] = (int16_t)e.w;
            nav->goal_h[k] = (int16_t)e.h;
        } else if (e.type == ENT_PLATFORM && e.w && e.h && nav->plat_count < MAX_PLATFORMS) {
            int p = nav->plat_count++;      /* in the order core/game.c reads them */
            nav->plat_x[p][0] = e.x;
            nav->plat_y[p][0] = e.y;
            nav->plat_x[p][1] = e.x2;
            nav->plat_y[p][1] = e.y2;
            nav->plat_w[p] = (int16_t)e.w;
            nav->plat_h[p] = (int16_t)e.h;
            nav->plat_oneway[p] = (e.flag & 1) != 0;
            nav->plat_speed[p] = (int32_t)e.param << 8;
        }
    }
    find_nodes(nav);
    for (int g = 0; g < NAV_GRAPHS; g++) {
        nav->graphs[g].edge_count = 0;
        nav->graphs[g].first[0] = 0;
    }
}

bool bot_nav_step(BotNav *nav, int n)
{
    while (!nav->done && n-- > 0) {
        if (nav->next >= nav->node_count) {
            nav->next = 0;
            if (++nav->graph == NAV_GRAPHS) {
                nav->done = true;
                break;
            }
            continue;
        }
        build_node(nav, nav->graph, nav->next++);
    }
    return nav->done;
}

/* ---- graph file ---------------------------------------------------------------- */

/* Cells, entities and tuning the graph was built from. */
static uint32_t nav_key(const Level *lv, const PlayerTuning *t)
{
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < (uint32_t)lv->cell_w * lv->cell_h; i++)
        h = (h ^ lv->cells[i]) * 0x01000193u;
    PlayerTuning m = *t;
    m.fruit_min = m.fruit_range = 0;    /* not movement */
    const int32_t *v = (const int32_t *)(const void *)&m;
    for (unsigned i = 0; i < sizeof m / sizeof *v; i++)
        h = (h ^ (uint32_t)v[i]) * 0x01000193u;
    for (uint32_t i = 0; i < (uint32_t)ENTITY_BYTES * lv->entity_count; i++)
        h = (h ^ lv->entities[i]) * 0x01000193u;
    return h;
}

static uint8_t *put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return p + 2; }
static unsigned get16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }

uint32_t bot_nav_size(const BotNav *nav)
{
    uint32_t n = NAV_HEADER_BYTES + 6u * nav->node_count;
    for (int g = 0; g < NAV_GRAPHS; g++)
        n += 2u * (nav->node_count + 1u) + NAV_EDGE_BYTES * nav->graphs[g].edge_count;
    return n;
}

void bot_nav_write(const BotNav *nav, uint8_t *out)
{
    uint32_t key = nav_key(&nav->lv, &nav->tuning);
    uint8_t *p = out;
    *p++ = 'N'; *p++ = 'A'; *p++ = 'V'; *p++ = '1';
    p = put16(p, key >> 16);
    p = put16(p, key & 0xffff);
    p = put16(p, nav->node_count);
    for (int g = 0; g < NAV_GRAPHS; g++)
        p = put16(p, nav->graphs[g].edge_count);
    for (int k = 0; k < TEAM_COUNT; k++) {
        p = put16(p, (uint16_t)nav->goal_x[k]);
        p = put16(p, (uint16_t)nav->goal_y[k]);
        p = put16(p, (uint16_t)nav->goal_w[k]);
        p = put16(p, (uint16_t)nav->goal_h[k]);
    }
    for (int i = 0; i < nav->node_count; i++) {
        p = put16(p, (uint16_t)nav->node_x[i]);
        p = put16(p, (uint16_t)nav->node_y[i]);
        *p++ = (uint8_t)nav->node_plat[i];
        *p++ = nav->node_end[i];
    }
    for (int g = 0; g < NAV_GRAPHS; g++) {
        const NavGraph *gr = &nav->graphs[g];
        for (int i = 0; i <= nav->node_count; i++)
            p = put16(p, gr->first[i]);
        for (int i = 0; i < gr->edge_count; i++) {
            const NavEdge *e = &gr->edges[i];
            p = put16(p, e->to);
            p = put16(p, e->cost);
            *p++ = e->kind;
            *p++ = (uint8_t)e->dir;
            *p++ = (uint8_t)e->air;
            *p++ = e->run;
            *p++ = e->hold;
            *p++ = e->dbl;
            *p++ = e->touch;
            *p++ = e->touch_cost;
        }
    }
}

int bot_nav_read(BotNav *nav, const Level *lv, const PlayerTuning *t, const uint8_t *data,
                 uint32_t size)
{
    if (size < NAV_HEADER_BYTES || data[0] != 'N' || data[1] != 'A' || data[2] != 'V' ||
        data[3] != '1')
        return 1;
    if (((uint32_t)get16(data + 4) << 16 | get16(data + 6)) != nav_key(lv, t))
        return 2;               /* built from other cells or tuning */
    nav->lv = *lv;
    nav->lv.solid_count = 0;
    nav->tuning = *t;
    nav->node_count = (uint16_t)get16(data + 8);
    for (int g = 0; g < NAV_GRAPHS; g++)
        nav->graphs[g].edge_count = (uint16_t)get16(data + 10 + 2 * g);
    for (int k = 0; k < TEAM_COUNT; k++) {
        const uint8_t *q = data + 10 + 2 * NAV_GRAPHS + 8 * k;
        nav->goal_x[k] = (int16_t)get16(q);
        nav->goal_y[k] = (int16_t)get16(q + 2);
        nav->goal_w[k] = (int16_t)get16(q + 4);
        nav->goal_h[k] = (int16_t)get16(q + 6);
    }
    if (nav->node_count >= NAV_MAX_NODES || nav->graphs[0].edge_count > NAV_MAX_EDGES ||
        nav->graphs[1].edge_count > NAV_MAX_EDGES || bot_nav_size(nav) != size)
        return 3;
    const uint8_t *p = data + NAV_HEADER_BYTES;
    for (int i = 0; i < nav->node_count; i++, p += 6) {
        nav->node_x[i] = (int16_t)get16(p);
        nav->node_y[i] = (int16_t)get16(p + 2);
        nav->node_plat[i] = (int8_t)p[4];
        nav->node_end[i] = p[5];
    }
    for (int g = 0; g < NAV_GRAPHS; g++) {
        NavGraph *gr = &nav->graphs[g];
        for (int i = 0; i <= nav->node_count; i++, p += 2)
            gr->first[i] = (uint16_t)get16(p);
        for (int i = 0; i < gr->edge_count; i++, p += NAV_EDGE_BYTES) {
            NavEdge *e = &gr->edges[i];
            e->to = (uint16_t)get16(p);
            e->cost = (uint16_t)get16(p + 2);
            e->kind = p[4];
            e->dir = (int8_t)p[5];
            e->air = (int8_t)p[6];
            e->run = p[7];
            e->hold = p[8];
            e->dbl = p[9];
            e->touch = p[10];
            e->touch_cost = p[11];
            if (e->to >= nav->node_count)
                return 3;
        }
    }
    nav->graph = NAV_GRAPHS;
    nav->next = 0;
    nav->done = true;
    return 0;
}

/* ---- planning ------------------------------------------------------------------ */

static uint32_t rnd(Bot *b)
{
    uint32_t x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return b->rng = x;
}

static int box_gap_x(int x, int w, int bx, int bw)
{
    return x + w <= bx ? bx - (x + w) : bx + bw <= x ? x - (bx + bw) : 0;
}

static bool node_in_box(const BotNav *nav, int n, int bx, int by, int bw, int bh)
{
    int x = nav->node_x[n], y = nav->node_y[n];
    return x < bx + bw && x + PLAYER_HIT_W > bx && y < by + bh && y + PLAYER_HIT_H > by;
}

static int edge_penalty(const Bot *b, int graph, int e)
{
    for (int i = 0; i < 8; i++)
        if (b->fail_age[i] && b->fail_edge[i] == e && b->fail_graph[i] == graph)
            return 240;
    return 0;
}

static void penalize(Bot *b, int graph, int e)
{
    int slot = 0;
    for (int i = 1; i < 8; i++)
        if (b->fail_age[i] < b->fail_age[slot])
            slot = i;
    b->fail_edge[slot] = (int16_t)e;
    b->fail_graph[slot] = (uint8_t)graph;
    b->fail_age[slot] = 255;
}

/* A* scratch (one plan at a time). */
static uint16_t as_g[NAV_MAX_NODES];
static int16_t as_edge[NAV_MAX_NODES], as_prev[NAV_MAX_NODES];
static uint8_t as_closed[NAV_MAX_NODES];
static int16_t as_path[NAV_MAX_NODES];
static uint32_t as_heap[NAV_MAX_EDGES + NAV_MAX_NODES];     /* f << 16 | node */
static int as_n;

static void heap_push(uint32_t v)
{
    int i = as_n++;
    while (i > 0 && as_heap[(i - 1) / 2] > v) {
        as_heap[i] = as_heap[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    as_heap[i] = v;
}

static uint32_t heap_pop(void)
{
    uint32_t top = as_heap[0], v = as_heap[--as_n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= as_n)
            break;
        if (c + 1 < as_n && as_heap[c + 1] < as_heap[c])
            c++;
        if (as_heap[c] >= v)
            break;
        as_heap[i] = as_heap[c];
        i = c;
    }
    as_heap[i] = v;
    return top;
}

/* A* from node start to a node standing in the goal box, or through goal
 * zone b->goal_touch (>= 0) on a move: the path then ends in node
 * nav->node_count, which stands for the zone. If neither can be reached,
 * to the reached node nearest the box. The path goes to b->path_*. */
static void plan(Bot *b, const BotNav *nav, int start, int graph)
{
    const NavGraph *gr = &nav->graphs[graph];
    fx speed = graph ? nav->tuning.carry_speed : nav->tuning.run_speed;
    int bx = b->goal_x, by = b->goal_y, bw = b->goal_w, bh = b->goal_h;
    /* No node in the box (a flag in the air, a player jumping): widen it
     * sideways and down. */
    for (int k = 0; k < 4; k++) {
        bool any = false;
        for (int i = 0; i < nav->node_count && !any; i++)
            any = node_in_box(nav, i, bx, by, bw, bh);
        if (any)
            break;
        bx -= TILE_W;
        bw += 2 * TILE_W;
        bh += 4 * TILE_H;
    }
    int zone = nav->node_count;
    for (int i = 0; i <= zone; i++) {
        as_g[i] = 0xffff;
        as_closed[i] = 0;
    }
    as_n = 0;
    as_g[start] = 0;
    as_edge[start] = as_prev[start] = -1;
    heap_push((uint32_t)start);
    int found = -1, nearest = start, nearest_h = 0x7fffffff;
    while (as_n) {
        uint32_t top = heap_pop();
        int n = (int)(top & 0xffff);
        if (as_closed[n])
            continue;
        as_closed[n] = 1;
        if (n == zone || node_in_box(nav, n, bx, by, bw, bh)) {
            found = n;
            break;
        }
        int gap = box_gap_x(nav->node_x[n], PLAYER_HIT_W, bx, bw) +
                  iabs(nav->node_y[n] - by);
        if (gap < nearest_h) {
            nearest_h = gap;
            nearest = n;
        }
        for (int e = gr->first[n]; e < gr->first[n + 1]; e++) {
            const NavEdge *ed = &gr->edges[e];
            int to = ed->to;
            uint32_t pen = (uint32_t)edge_penalty(b, graph, e);
            if (b->goal_touch >= 0 && (ed->touch & (1u << b->goal_touch))) {
                uint32_t c = as_g[n] + ed->touch_cost + pen;
                if (c < as_g[zone] && c <= 0xfff0 && as_n < (int)(sizeof as_heap / sizeof as_heap[0])) {
                    as_g[zone] = (uint16_t)c;
                    as_edge[zone] = (int16_t)e;
                    as_prev[zone] = (int16_t)n;
                    heap_push(c << 16 | (uint32_t)zone);
                }
                continue;
            }
            uint32_t c = as_g[n] + ed->cost + pen;
            if (as_closed[to] || c >= as_g[to] || c > 0xfff0)
                continue;
            as_g[to] = (uint16_t)c;
            as_edge[to] = (int16_t)e;
            as_prev[to] = (int16_t)n;
            uint32_t h = (uint32_t)(FX(box_gap_x(nav->node_x[to], PLAYER_HIT_W, bx, bw)) / speed);
            uint32_t f = c + h;
            if (as_n < (int)(sizeof as_heap / sizeof as_heap[0]))
                heap_push((f > 0xffff ? 0xffff : f) << 16 | (uint32_t)to);
        }
    }
    int end = found >= 0 ? found : nearest;
    int len = 0;
    for (int n = end; n >= 0; n = as_prev[n])
        as_path[len++] = (int16_t)n;                /* end first */
    int keep = len > BOT_PATH_MAX ? BOT_PATH_MAX : len;     /* the start of a long path */
    for (int k = 0; k < keep; k++)
        b->path_node[k] = as_path[len - 1 - k];
    for (int k = 0; k + 1 < keep; k++)
        b->path_edge[k] = as_edge[b->path_node[k + 1]];
    b->path_edge[keep - 1] = -1;
    b->path_len = (uint8_t)keep;
    b->path_pos = 0;
    b->path_graph = (uint8_t)graph;
    b->replan = false;
    b->plan_x = (int16_t)b->goal_x;
    b->plan_y = (int16_t)b->goal_y;
}

/* ---- aims ------------------------------------------------------------------------ */

void bot_init(Bot *b, int player, uint32_t seed)
{
    *b = (Bot){0};
    b->rng = seed ? seed : 0x6d2b79f5u;
    b->player = (uint8_t)player;
    b->move = -1;
    b->aim_target = -1;
    b->last_contact = -1;
    b->swings = BOT_SWINGS;
    b->replan = true;
}

static bool alive(const Game *g, int t)
{
    const Player *p = &g->players[t];
    return !p->dead && p->state != PS_HURT && !g->caught[t];
}

static int cx(const Body *b) { return b->x + b->w / 2; }
static int cy(const Body *b) { return b->y + b->h / 2; }

/* Rough travel cost between two points, px: climbing costs more. */
static int dist(int ax, int ay, int bx, int by)
{
    return iabs(ax - bx) + 2 * iabs(ay - by);
}

static void set_goal(Bot *b, int x, int y, int w, int h)
{
    b->goal_x = (int16_t)x;
    b->goal_y = (int16_t)y;
    b->goal_w = (int16_t)w;
    b->goal_h = (int16_t)h;
}

/* Is a teammate of player t (alive, not carrying) nearer to (x, y)? */
static bool teammate_nearer(const Game *g, int t, int x, int y)
{
    const Body *pb = &g->players[t].body;
    int d = dist(cx(pb), cy(pb), x, y);
    for (int o = 0; o < g->player_count; o++) {
        const Body *ob = &g->players[o].body;
        if (o != t && game_team(o) == game_team(t) && alive(g, o) && g->carrying[o] < 0 &&
            dist(cx(ob), cy(ob), x, y) < d)
            return true;
    }
    return false;
}

static void pick_aim(Bot *b, const Game *g)
{
    int t = b->player, team = game_team(t);
    const Body *pb = &g->players[t].body;
    int px = cx(pb), py = cy(pb);
    int aim = BOT_CAPTURE, target = -1, best = 0x7fffffff;
    if (g->carrying[t] >= 0) {
        const Flag *f = &g->flags[g->carrying[t]];
        aim = f->team == team ? BOT_RECOVER : BOT_CAPTURE;
        target = g->carrying[t];
    } else {
        int placed = 0;                 /* own flags in the enemy goal */
        for (int i = 0; i < g->flag_count; i++)
            placed += g->flags[i].team == team && g->flags[i].state == FS_PLACED;
        /* HUNT: an enemy carrying an own flag. */
        for (int o = 0; o < g->player_count && !b->hunt_rest; o++) {
            if (game_team(o) == team || g->carrying[o] < 0 || !alive(g, o) ||
                g->flags[g->carrying[o]].team != team)
                continue;
            const Body *ob = &g->players[o].body;
            int d = dist(px, py, cx(ob), cy(ob)) - 160 - 60 * placed;
            if (teammate_nearer(g, t, cx(ob), cy(ob)))
                d += 200;
            if (d < best) {
                best = d;
                aim = BOT_HUNT;
                target = o;
            }
        }
        for (int i = 0; i < g->flag_count; i++) {
            const Flag *f = &g->flags[i];
            int fx = cx(&f->body), fy = cy(&f->body), d = dist(px, py, fx, fy);
            if (teammate_nearer(g, t, fx, fy))
                d += 160;
            if (f->team == team && (f->state == FS_DROPPED ||
                                    (f->state == FS_PLACED && f->holder != team))) {
                d -= placed >= FLAGS_PER_TEAM - 1 ? 200 : 40;       /* RECOVER */
                if (d < best) {
                    best = d;
                    aim = BOT_RECOVER;
                    target = i;
                }
            } else if (f->team != team && (f->state == FS_HOME || f->state == FS_DROPPED)) {
                if (d < best) {                                     /* CAPTURE */
                    best = d;
                    aim = BOT_CAPTURE;
                    target = i;
                }
            }
        }
        /* DEFEND: an enemy near the own flags at home, the bot near too. */
        int hx = (g->home_x0[team] + g->home_x1[team]) / 2, hy = (g->home_y0[team] + g->home_y1[team]) / 2;
        int home_d = dist(px, py, hx, hy);
        for (int o = 0; o < g->player_count; o++) {
            const Body *ob = &g->players[o].body;
            if (game_team(o) == team || g->carrying[o] >= 0 || !alive(g, o) ||
                dist(cx(ob), cy(ob), hx, hy) > 150 || home_d > 260)
                continue;
            int d = home_d - 100;
            if (d < best) {
                best = d;
                aim = BOT_DEFEND;
                target = o;
            }
        }
    }
    if (aim != b->aim || target != b->aim_target)
        b->replan = true;
    if (aim != BOT_HUNT)
        b->hunt_timer = 0;
    b->aim = (uint8_t)aim;
    b->aim_target = (int8_t)target;
}

/* The box to go to for the aim, this frame. */
static void aim_goal(Bot *b, const Game *g)
{
    int t = b->player, team = game_team(t), k = b->aim_target;
    b->goal_touch = -1;
    if (g->carrying[t] >= 0) {
        if (g->flags[g->carrying[t]].team != team) {
            set_goal(b, g->goal_x[team], g->goal_y[team], g->goal_w[team], g->goal_h[team]);
            b->goal_touch = (int8_t)team;
        } else
            set_goal(b, g->home_x0[team], g->home_y0[team], g->home_x1[team] - g->home_x0[team],
                     g->home_y1[team] - g->home_y0[team]);
    } else if (k >= 0 && (b->aim == BOT_HUNT || b->aim == BOT_DEFEND)) {
        const Body *ob = &g->players[k].body;
        set_goal(b, ob->x - 8, ob->y, ob->w + 16, ob->h);
    } else if (k >= 0) {
        const Body *fb = &g->flags[k].body;
        set_goal(b, fb->x, fb->y, fb->w, fb->h);
    } else {                            /* nothing to do: by the own flags */
        set_goal(b, g->home_x0[team], g->home_y0[team], g->home_x1[team] - g->home_x0[team],
                 g->home_y1[team] - g->home_y0[team]);
    }
    if (iabs(b->goal_x - b->plan_x) > TILE_W || iabs(b->goal_y - b->plan_y) > TILE_H)
        b->replan = true;
}

/* ---- moving ------------------------------------------------------------------------ */

static uint16_t dir_button(int d) { return d < 0 ? BTN_LEFT : d > 0 ? BTN_RIGHT : 0; }

/* Walk toward x, letting go early enough to stop there (on ice too). */
static uint16_t walk_to(const Game *g, const Player *p, int x)
{
    const Body *pb = &p->body;
    int dx = x - pb->x;
    if (iabs(dx) <= 1)
        return 0;
    int d = dx > 0 ? 1 : -1;
    int cyf = (pb->y + pb->h) >> TILE_SHIFT_Y;
    bool ice = level_surface(&g->lv, cx(pb) >> TILE_SHIFT_X, cyf) == SURFACE_ICE;
    fx decel = ice ? g->tuning.ice_decel : g->tuning.run_decel;
    if ((pb->vx > 0) == (d > 0) && pb->vx != 0) {
        int stop = (int)(((int64_t)pb->vx * pb->vx / (2 * decel)) >> 16);
        if (stop >= iabs(dx))
            return 0;
    }
    return dir_button(d);
}

static void reset_move(Bot *b)
{
    b->move = -1;
    b->settle = 0;
    b->wiggle = 0;
    b->look = 0;
    b->blocked = 0;
}

/* Botch a jump on purpose: no double jump, or a short hop. */
static void botch(Bot *b, NavEdge *e)
{
    if (e->dbl) {
        e->dbl = 0;
        e->hold = 0xff;
    } else if (e->hold == 0xff) {
        e->hold = (uint8_t)(4 + rnd(b) % 6);
    } else {
        e->air = 0;
    }
}

/* ---- players in the way -------------------------------------------------------- */

/* Body x at which a walk from x in direction d along row y (body top) runs
 * out of floor (or meets a wall). */
static int ledge_x(const Level *lv, int x, int y, int d)
{
    int cy = (y + PLAYER_HIT_H) >> TILE_SHIFT_Y;
    int c = (x + PLAYER_HIT_W / 2) >> TILE_SHIFT_X;
    for (int nc = c + d; nc >= 0 && nc < lv->cell_w; nc += d) {
        if (level_cell(lv, nc, cy - 1) == CELL_SOLID ||
            (level_cell(lv, nc, cy) != CELL_SOLID && !level_cell_platform(lv, nc, cy)))
            break;
        c = nc;
    }
    return d > 0 ? (c + 1) * TILE_W : c * TILE_W - PLAYER_HIT_W;
}

/* Body x at which move e, at frame f with the body at x on row y, leaves
 * the ground (x: no run on the ground first). */
static int takeoff_x(const BotNav *nav, const NavEdge *e, int f, int x, int y)
{
    if (e->kind == NAV_FALL)
        return ledge_x(&nav->lv, x, y, e->dir);
    if (e->kind == NAV_JUMP && e->dir && f < e->run)
        return x + e->dir * ((e->run - f) * nav->tuning.run_speed >> 16);
    return x;
}

/* A player in the way of bot b walking from body x x0 to x1 on its row:
 * ahead within BOT_LOOK px, standing or coming the other way; unless b
 * carries a flag, not an enemy carrier or the enemy it is after (it
 * fights those). -1 if none. */
static int in_way(const Bot *b, const Game *g, int x0, int x1)
{
    int t = b->player;
    const Body *pb = &g->players[t].body;
    int d = x1 > x0 ? 1 : x1 < x0 ? -1 : 0;
    if (!d)
        return -1;
    if (iabs(x1 - x0) > BOT_LOOK)
        x1 = x0 + d * BOT_LOOK;
    int lo = d > 0 ? x0 : x1, hi = (d > 0 ? x1 : x0) + PLAYER_HIT_W;
    for (int o = 0; o < g->player_count; o++) {
        const Body *ob = &g->players[o].body;
        int ov = ob->vx > 0 ? 1 : ob->vx < 0 ? -1 : 0;
        if (o == t || !alive(g, o) || (ob->x - x0) * d <= 0 || ob->x >= hi ||
            ob->x + ob->w <= lo || ob->y >= pb->y + pb->h || ob->y + ob->h <= pb->y || ov == d)
            continue;
        if (game_team(o) != game_team(t) && g->carrying[t] < 0 &&
            (g->carrying[o] >= 0 ||
             ((b->aim == BOT_HUNT || b->aim == BOT_DEFEND) && b->aim_target == o)))
            continue;
        return o;
    }
    return -1;
}

/* Walking into a player (direction button d): jump on that way, back off
 * (walking, as far as the floor goes; else jumping) or wait and go another
 * way (edge, if any, costs more). Plans again. */
static void unstick(Bot *b, const BotNav *nav, const Game *g, int edge, uint16_t d)
{
    const Body *pb = &g->players[b->player].body;
    uint16_t back = d ^ (BTN_LEFT | BTN_RIGHT);
    unsigned r = rnd(b) % 4, frames = 0;
    if (r == 3 && edge < 0)
        r = 2;
    if (r == 2) {                       /* well short of the ledge (it slides on) */
        frames = (unsigned)iabs(ledge_x(&nav->lv, pb->x, pb->y, back & BTN_LEFT ? -1 : 1) - pb->x) / 4;
        if (frames > 20 + rnd(b) % 41)
            frames = 20 + rnd(b) % 41;
    }
    reset_move(b);
    b->stuck = 0;
    b->replan = true;
    if (r < 2 || (r == 2 && frames < 8)) {
        b->wiggle = 30;
        b->wiggle_buttons = (uint16_t)((r < 2 ? d : back) | BTN_JUMP);
    } else if (r == 2) {
        b->wiggle = (uint8_t)frames;
        b->wiggle_buttons = back;
    } else {
        if (edge >= 0)
            penalize(b, b->path_graph, edge);
        b->wiggle = (uint8_t)(5 + rnd(b) % 16);
        b->wiggle_buttons = 0;
    }
}

/* A player in the way of the walk from x0 to x1 (edge, if any): once seen
 * for the reaction time, go round it (edge costs more, plan again), jump
 * over it (when near) or wait for it (when not touching). true if the bot
 * does so now. */
static bool dodge(Bot *b, const Game *g, int edge, int x0, int x1)
{
    int o = in_way(b, g, x0, x1);
    if (o < 0) {
        b->look = 0;
        return false;
    }
    if (b->look++ == 0)
        b->look_need = (uint8_t)(BOT_LOOK_REACT + rnd(b) % 17);
    if (b->look < b->look_need)
        return false;
    int d = x1 > x0 ? 1 : -1, gap = iabs(g->players[o].body.x - x0) - PLAYER_HIT_W;
    unsigned r = rnd(b) % 4;
    if (r < 2 && edge < 0)
        r = 2;
    if (r == 3 && gap <= 2)             /* touching: waiting does nothing */
        r = 2;
    if (r == 2 && gap > 2 * TILE_W)
        r = 3;
    reset_move(b);
    if (r < 2) {
        penalize(b, b->path_graph, edge);
        b->replan = true;
    } else if (r == 2) {
        b->wiggle = 30;
        b->wiggle_buttons = (uint16_t)(dir_button(d) | BTN_JUMP);
        b->replan = true;
    } else {
        b->wiggle = (uint8_t)(10 + rnd(b) % 31);
        b->wiggle_buttons = 0;
    }
    return true;
}

/* The node a standing body is at: a static node, or the node of the moving
 * platform it stands on while that waits at an end; -1 if none. *riding:
 * that platform, or -1. */
static int node_of(const BotNav *nav, const Game *g, const Body *pb, int *riding)
{
    *riding = -1;
    for (int i = 0; i < g->platform_count; i++) {
        const Solid *s = &g->lv.solids[i];
        const Platform *pl = &g->platforms[i];
        if (pb->y + pb->h != s->y || pb->x >= s->x + s->w || pb->x + pb->w <= s->x)
            continue;
        *riding = i;
        int k = s->x == pl->x0 && s->y == pl->y0 ? 0 : s->x == pl->x1 && s->y == pl->y1 ? 1 : -1;
        return k < 0 ? -1 : plat_node(nav, i, k);
    }
    return node_near(nav, pb->x, pb->y, SNAP_X);
}

/* Will platform i be standing at end k in `frames` frames? */
static bool plat_at(const Game *g, int i, int k, int frames)
{
    const Platform *pl = &g->platforms[i];
    int x, y;
    game_platform_ahead(g, i, frames, &x, &y);
    return k ? x == pl->x1 && y == pl->y1 : x == pl->x0 && y == pl->y0;
}

/* Can move e from node `from` start now: a platform it leaves still waits
 * at its end for the run-up and the takeoff, a platform it lands on will
 * be at that end on landing? */
static bool move_timed(const BotNav *nav, const Game *g, int from, const NavEdge *e)
{
    int p = nav->node_plat[from], q = nav->node_plat[e->to];
    if (p >= 0 && !plat_at(g, p, nav->node_end[from], e->run + 6))
        return false;
    if (q >= 0 && e->to != from && (!plat_at(g, q, nav->node_end[e->to], e->cost + 1) ||
                                    !plat_at(g, q, nav->node_end[e->to], e->cost + 4)))
        return false;
    return true;
}

static uint16_t navigate(Bot *b, const BotNav *nav, const Game *g)
{
    int t = b->player;
    const Player *p = &g->players[t];
    const Body *pb = &p->body;
    int graph = g->carrying[t] >= 0;
    if (b->move >= 0) {
        if (p->grounded && b->move_air && p->body.vx == 0) {     /* landed, at rest */
            const NavEdge *e = &nav->graphs[b->path_graph].edges[b->move];
            int riding, n = node_of(nav, g, pb, &riding);
            bool scored = e->touch && graph == 0 && b->path_graph == 1;
            if (e->touch && b->path_graph == 1) {
                if (!scored && !b->botched)
                    penalize(b, b->path_graph, b->move);
                b->replan = true;
            } else if (n != e->to) {
                if (!b->botched)
                    penalize(b, b->path_graph, b->move);
                b->replan = true;
            }
            b->move = -1;
        } else {
            if (!p->grounded)
                b->move_air = true;
            else if (b->move_air)
                b->move_landed = true;
            uint16_t h = b->move_landed ? 0 : bot_move_buttons(&b->move_edge, b->move_f);
            uint16_t hd = h & (BTN_LEFT | BTN_RIGHT);
            if (p->grounded && !b->move_air && h != 0xffff && !hd &&
                !(h & (BTN_JUMP | BTN_DOWN)) && pb->vx == 0 && b->move_f) {
                /* Its run done, at rest, not off: started off its node, it
                 * will not leave the ground. */
                penalize(b, b->path_graph, b->move);
                reset_move(b);
                b->replan = true;
                return 0;
            }
            if (p->grounded && !b->move_air && h != 0xffff && hd) {
                /* Still on the ground: a player in the way ahead, or walked into. */
                int d = hd == BTN_LEFT ? -1 : 1;
                int x1 = d == b->move_edge.dir
                             ? takeoff_x(nav, &b->move_edge, b->move_f, pb->x, pb->y) : pb->x;
                if (dodge(b, g, b->move, pb->x, x1))
                    return 0;
                if (pb->x != b->last_x || b->move_f < 2)
                    b->blocked = 0;
                else if (++b->blocked >= b->block_need) {
                    unstick(b, nav, g, b->move, hd);
                    return 0;
                }
            }
            b->move_f++;
            if (b->move_landed && b->move_f < MOVE_MAX_FRAMES)
                return 0;
            if (h != 0xffff && !p->knock)
                return h;
            b->move = -1;
            b->replan = true;
            return 0;
        }
    }
    if (b->wiggle) {
        b->wiggle--;
        return b->wiggle_buttons;
    }
    if (!p->grounded)
        return 0;
    int riding, cur = node_of(nav, g, pb, &riding);
    if (cur < 0 && riding >= 0)         /* on a moving platform between its ends: ride */
        return 0;
    if (cur < 0) {                      /* off the graph: on a player, or between nodes */
        int best = -1, bd = 0x7fffffff;
        for (int i = 0; i < nav->node_count; i++) {
            int d = iabs(nav->node_x[i] - pb->x);
            if (nav->node_y[i] == pb->y && nav->node_plat[i] < 0 && d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best >= 0 && bd > 1)
            return walk_to(g, p, nav->node_x[best]);
        /* Nothing on this row (standing on a player): walk off toward the goal. */
        return dir_button(b->goal_x + b->goal_w / 2 < pb->x + pb->w / 2 ? -1 : 1);
    }
    if (graph != b->path_graph || b->path_len == 0)
        b->replan = true;
    int k = -1;
    for (int i = b->path_pos; i < b->path_len && !b->replan; i++)
        if (b->path_node[i] == cur) {
            k = i;
            break;
        }
    if (k < 0) {
        plan(b, nav, cur, graph);
        k = 0;
    }
    b->path_pos = (uint8_t)k;
    const NavGraph *gr = &nav->graphs[b->path_graph];
    int j = k;
    while (j + 1 < b->path_len && gr->edges[b->path_edge[j]].kind == NAV_WALK)
        j++;
    if (j + 1 >= b->path_len) {         /* the last node: on to the goal box itself */
        int gx = b->goal_x + b->goal_w / 2 - PLAYER_HIT_W / 2;
        if (b->aim == BOT_HUNT || b->aim == BOT_DEFEND)     /* beside the enemy, facing it */
            gx += pb->x < gx ? -(PLAYER_HIT_W / 2 + 8) : PLAYER_HIT_W / 2 + 8;
        if (j > k && iabs(nav->node_x[b->path_node[j]] - pb->x) > TILE_W * 2)
            gx = nav->node_x[b->path_node[j]];
        return walk_to(g, p, gx);
    }
    int from = b->path_node[j];
    const NavEdge *next = &gr->edges[b->path_edge[j]];
    if (next->kind == NAV_RIDE && j == k)
        return 0;                       /* wait for the platform to go */
    if (riding < 0 && nav->node_plat[from] < 0) {
        /* A player in the way of the walk to the takeoff and the move's run. */
        int x0 = pb->x, x1 = nav->node_x[from];
        if (j == k && iabs(x1 - x0) <= SNAP_X)
            x1 = x0;                    /* near enough to start from here (below) */
        int d = x1 != x0 ? (x1 > x0 ? 1 : -1) : next->dir;
        if (d && next->dir == d)
            x1 = takeoff_x(nav, next, 0, x1, pb->y);
        if (dodge(b, g, b->path_edge[k], x0, x1))
            return 0;
    }
    /* Another player may hold the node (or a platform's middle): start from
     * near it. */
    int slack = b->stuck > 6 ? (nav->node_plat[from] >= 0 ? 10 : SNAP_X) : 2;
    if (j > k || iabs(pb->x - nav->node_x[from]) > slack || iabs(pb->vx) > FX(1) / 2)
        return walk_to(g, p, nav->node_x[from]);
    /* At the takeoff node: wait a little (and for moving platforms to be
     * where the move needs them), then play the move (this frame standing
     * still, as the graph's moves start). */
    if (!move_timed(nav, g, from, next)) {
        if (++b->wait > 400) {          /* the platform never comes: try another way */
            b->wait = 0;
            penalize(b, b->path_graph, b->path_edge[j]);
            b->replan = true;
        }
        b->settle = 0;
        return 0;
    }
    b->wait = 0;
    if (b->settle == 0)
        b->settle = (uint8_t)(1 + rnd(b) % 6);
    if (--b->settle)
        return 0;
    if (!move_timed(nav, g, from, next))
        return 0;
    b->move = b->path_edge[j];
    b->move_edge = gr->edges[b->move];
    b->move_f = 0;
    b->move_air = b->move_landed = false;
    b->botched = false;
    b->blocked = 0;
    b->block_need = (uint8_t)(10 + rnd(b) % 16);
    if (b->move_edge.kind == NAV_JUMP) {
        int team = game_team(t), to = b->move_edge.to;
        bool into_goal = graph && ((b->move_edge.touch & (1u << team)) ||
                                   node_in_box(nav, to, g->goal_x[team], g->goal_y[team],
                                               g->goal_w[team], g->goal_h[team]));
        if ((int)(rnd(b) % 100) < (into_goal ? BOT_GOAL_MISS : BOT_JUMP_MISS)) {
            botch(b, &b->move_edge);
            b->botched = true;
        }
    }
    return 0;
}

/* ---- fighting ---------------------------------------------------------------------- */

static bool box_hits(int ax, int ay, int aw, int ah, const Body *o)
{
    return ax < o->x + o->w && ax + aw > o->x && ay < o->y + o->h && ay + ah > o->y;
}

/* Which attack of player p would hit body o now: ATK_* + 1 with the side
 * (-1, 1) for a side kick in *side, or 0. */
static int reach(const Player *p, const Body *o, int *side)
{
    const Body *b = &p->body;
    if (box_hits(b->x + b->w, b->y + 3, 16, 10, o)) { *side = 1; return ATK_SIDE + 1; }
    if (box_hits(b->x - 16, b->y + 3, 16, 10, o)) { *side = -1; return ATK_SIDE + 1; }
    if (box_hits(b->x - 4, b->y - 10, b->w + 8, 12, o)) return ATK_UP + 1;
    if (!p->grounded && box_hits(b->x - 4, b->y + b->h - 2, b->w + 8, 12, o)) return ATK_DOWN + 1;
    return 0;
}

static uint16_t fight(Bot *b, const Game *g, uint16_t buttons)
{
    int t = b->player;
    const Player *p = &g->players[t];
    if (g->carrying[t] >= 0 || p->attack_cooldown || p->attack_timer || p->state != PS_NORMAL ||
        p->knock)
        return buttons;
    int victim = -1, kind = 0, side = 0;
    for (int o = 0; o < g->player_count; o++) {
        int s = 0, k;
        if (game_team(o) == game_team(t) || !alive(g, o) || g->players[o].invuln)
            continue;
        if ((k = reach(p, &g->players[o].body, &s)) &&
            (victim < 0 || (g->carrying[o] >= 0 && g->carrying[victim] < 0))) {
            victim = o;
            kind = k;
            side = s;
        }
    }
    if (victim < 0) {
        b->react = 0;
        b->last_contact = -1;
        return buttons;
    }
    if (victim != b->last_contact) {
        b->last_contact = (int8_t)victim;
        b->react = 0;
        b->react_need = (uint8_t)(BOT_REACT - 4 + rnd(b) % 9);
        b->fight_roll = (int)(rnd(b) % 100) < BOT_FIGHT ||
                        (b->aim == BOT_DEFEND && b->aim_target == victim);
    }
    bool carrier = g->carrying[victim] >= 0;
    if ((!carrier && !b->fight_roll) || ++b->react < b->react_need || !b->swings ||
        (b->held & BTN_ATTACK) || (b->move >= 0 && !carrier))
        return buttons;
    b->swings--;
    b->react = 0;
    if (b->swing_timer == 0)
        b->swing_timer = BOT_SWING_FRAMES;
    buttons &= (uint16_t)~(BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN | BTN_JUMP);
    if (kind == ATK_SIDE + 1)
        buttons |= dir_button(side);
    else
        buttons |= kind == ATK_UP + 1 ? BTN_UP : BTN_DOWN;
    return (uint16_t)(buttons | BTN_ATTACK);
}

/* ---- per frame ----------------------------------------------------------------------- */

InputState bot_input(Bot *b, const BotNav *nav, const Game *g)
{
    InputState in = {BTN_CONNECTED, 0, 0};
    int t = b->player;
    if (!nav->done || g->title || g->paused || g->winner || g->countdown > COUNTDOWN_GO ||
        t >= g->player_count) {
        reset_move(b);
        b->replan = true;
        b->held = 0;
        return in;
    }
    const Player *p = &g->players[t];
    const Body *pb = &p->body;
    for (int i = 0; i < 8; i++)
        if (b->fail_age[i])
            b->fail_age[i]--;
    if (b->swing_timer && --b->swing_timer == 0 && b->swings < BOT_SWINGS) {
        b->swings++;
        if (b->swings < BOT_SWINGS)
            b->swing_timer = BOT_SWING_FRAMES;
    }
    if (b->hunt_rest)
        b->hunt_rest--;
    if (b->aim == BOT_HUNT && ++b->hunt_timer >= BOT_HUNT_FRAMES) {
        b->hunt_rest = BOT_HUNT_REST;
        b->hunt_timer = 0;
        b->think = 0;
    }
    /* Sent home, respawned or carried off: start over. */
    if (iabs(pb->x - b->last_x) > 32 || iabs(pb->y - b->last_y) > 32) {
        reset_move(b);
        b->replan = true;
    }
    if (p->dead || p->state == PS_HURT || g->caught[t]) {
        reset_move(b);
        b->held = 0;
        b->last_x = pb->x;
        b->last_y = pb->y;
        return in;
    }
    bool carrying = g->carrying[t] >= 0;
    if (b->think == 0 || carrying != b->was_carrying) {
        b->think = BOT_THINK;
        pick_aim(b, g);
    }
    b->think--;
    b->was_carrying = carrying;
    aim_goal(b, g);
    uint16_t buttons = navigate(b, nav, g);
    /* Stuck (a player in the way): trying to walk but not moving for a
     * while (random; frames walk_to lets go to stop count too): get out
     * (unstick). */
    if (b->move < 0 && !b->wiggle && p->grounded &&
        ((buttons & (BTN_LEFT | BTN_RIGHT)) || b->stuck) && pb->x == b->last_x) {
        if (b->stuck == 0)
            b->stuck_need = (uint8_t)(10 + rnd(b) % 21);
        if (buttons & (BTN_LEFT | BTN_RIGHT))
            b->stuck_dir = buttons & (BTN_LEFT | BTN_RIGHT);
        if (++b->stuck > b->stuck_need) {
            int e = b->path_pos + 1 < b->path_len ? b->path_edge[b->path_pos] : -1;
            unstick(b, nav, g, e, b->stuck_dir);
            buttons = 0;                /* a release before the jump */
        }
    } else {
        b->stuck = 0;
    }
    buttons = fight(b, g, buttons);
    /* A press needs a release before it. */
    if (b->move < 0 && (b->held & BTN_JUMP) && (buttons & BTN_JUMP) && !b->wiggle)
        buttons &= (uint16_t)~BTN_JUMP;
    b->held = buttons;
    b->last_x = pb->x;
    b->last_y = pb->y;
    in.buttons |= buttons;
    return in;
}
