/* Route finder: breadth-first search over standing positions in a level,
 * with the real core movement, for scripting replays and checking that a
 * level can be won (tools/make_replays.py).
 *
 * usage: routefind LEVEL REPLAY SIDE TARGET
 *   REPLAY  replay so far: played through game_tick, so the search starts
 *           from player SIDE's exact state (at rest), carrying or not, with
 *           the replay's tuning
 *   SIDE    0 = P1, 1 = P2, 2 = P3, 3 = P4
 *   TARGET  flags1 | flags2 (touch a home flag of team 1/2),
 *           goal1 | goal2 (touch the goal zone of team 1/2),
 *           flag:I (touch flag I at home; Game.flags order),
 *           rest:X0,X1,Y (come to rest with X0 <= x <= X1 at y = Y),
 *           ride:I (come to rest standing on moving platform I)
 * A carrier moves at tuning.carry_speed. Wind is left out (routes are
 * followed between gusts). The other player is a fixed solid
 * box where it stands at the end of the replay (it must keep still while
 * the route is followed), as the game makes it solid to the moving one;
 * so are the others in a game of more than two.
 * On a level with moving platforms, where a move goes depends on
 * when it starts, every move runs the whole game (game_tick, the other
 * player idle) from the exact game state, so routes include riding and
 * pushing; a move in which the player is squished or hurt fails.
 *
 * A move starts at rest: hold DIR until the body x crosses TX (walking off
 * edges included; the jump then uses coyote time), then
 *   fall    keep holding DIR until grounded
 *   jump    jump held until grounded
 *   dK      jump held K-2 frames, released 1, then the double jump held
 *           until grounded (K = frame of the double jump press)
 *   drop    down 2 frames, down + jump 2 frames (through a one-way slab),
 *           then DIR until grounded
 * and stop (no input) until at rest. The air direction is DIR, or none
 * (AIR "-"). Each move continues from the exact player state the last one
 * ended in (sub-pixel remainders included), as the game does. Prints one
 * line per move: "DIR TX KIND AIR X Y" (X Y: where
 * the move ends at rest, or touches the target), or "none". Exit 1 if no
 * route.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "replay.h"

#define MAX_NODES 4096
#define QX 4                    /* positions closer than this are one node */

typedef struct {
    Player p;                   /* at rest */
    Game g;                     /* game_mode: the whole game, p = g.players[side] */
    int parent;
    char move[48];
} Node;

static bool game_mode;
static int side;
static Level lv;
static PlayerTuning tun;
static int targets[3][4], ntargets;     /* target rectangles: home flags or a goal */
static int rest_x0, rest_x1, rest_y = -1, ride = -1;
static Node nodes[MAX_NODES];
static int nnodes;

static bool touches(const Body *b)
{
    for (int i = 0; i < ntargets; i++) {
        int *r = targets[i];
        if (b->x < r[0] + r[2] && b->x + b->w > r[0] && b->y < r[1] + r[3] && b->y + b->h > r[1])
            return true;
    }
    return false;
}

static uint16_t prev;
static Game sim;                /* game_mode: the game the move runs in */

/* One frame with held input. false if the player was squished or hurt (game_mode). */
static bool tick(Player *p, uint16_t held)
{
    if (game_mode) {
        InputState in[MAX_PLAYERS] = {{BTN_CONNECTED, 0, 0}, {BTN_CONNECTED, 0, 0}};
        in[side].buttons |= held;
        game_tick(&sim, in);
        *p = sim.players[side];
        return !(sim.sfx & SFX_BIT(SFX_HIT)) && p->state != PS_HURT;
    }
    InputState in = {(uint16_t)(held | BTN_CONNECTED), 0, 0};
    uint16_t pressed = held & (uint16_t)~prev;
    prev = held;
    player_tick(p, &lv, &tun, &in, pressed);
    return true;
}

enum { K_FALL, K_JUMP, K_DROP, K_DOUBLE };

/* Simulate one move from rest in node from. Returns true if it ends at rest
 * (state in *out, and in sim for game_mode) or touches the target (*hit set),
 * false if it fails. */
static bool simulate(const Node *from, int dir, int txs, int kind, int k, bool air_same,
                     Player *out, bool *hit)
{
    Player p = from->p;
    if (game_mode)
        sim = from->g;
    prev = 0;
    *hit = false;
    uint16_t d = dir < 0 ? BTN_LEFT : BTN_RIGHT;
    uint16_t air = air_same ? d : 0;
    int f = 0;
    if (!tick(&p, 0) || !p.grounded)
        return false;
    while (dir < 0 ? p.body.x > txs : p.body.x < txs) {
        if (!tick(&p, d))
            return false;
        if (touches(&p.body)) { *hit = true; goto done; }
        if (++f > 300 || p.body.y >= lv.px_h)
            return false;
        if (p.body.vx == 0 && p.grounded && f > 4)
            return false;               /* blocked by a wall */
    }
    if (kind == K_DROP) {
        if (!p.grounded)
            return false;
        if (!tick(&p, BTN_DOWN) || !tick(&p, BTN_DOWN) || !tick(&p, BTN_DOWN | BTN_JUMP) ||
            !tick(&p, BTN_DOWN | BTN_JUMP))
            return false;
    }
    for (int j = 0; j < 400; j++) {
        uint16_t h = air;
        if (kind == K_JUMP || (kind == K_DOUBLE && (j < k - 1 || j >= k)))
            h |= BTN_JUMP;
        if (!tick(&p, h))
            return false;
        if (touches(&p.body)) { *hit = true; goto done; }
        if (p.body.y >= lv.px_h)
            return false;
        if (p.grounded && j > 1)
            break;
    }
    if (!p.grounded)
        return false;
    for (int j = 0; j < 60 && (p.body.vx || !p.grounded); j++) {
        if (!tick(&p, 0))
            return false;
        if (touches(&p.body)) { *hit = true; goto done; }
    }
done:
    *out = p;
    return true;
}

static bool seen(int x, int y)
{
    for (int i = 0; i < nnodes; i++)
        if (nodes[i].p.body.y == y && abs(nodes[i].p.body.x - x) < QX)
            return true;
    return false;
}

static void print_path(int i)
{
    if (nodes[i].parent >= 0) {
        print_path(nodes[i].parent);
        printf("%s %d %d\n", nodes[i].move, nodes[i].p.body.x, nodes[i].p.body.y);
    }
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: routefind LEVEL REPLAY SIDE TARGET\n");
        return 2;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    rewind(fp);
    uint8_t *data = malloc((size_t)n);
    if (fread(data, 1, (size_t)n, fp) != (size_t)n) return 2;
    fclose(fp);
    if (level_load(&lv, data, (uint32_t)n)) { fprintf(stderr, "bad level\n"); return 2; }
    Replay rp;
    if (replay_load(&rp, argv[2])) { fprintf(stderr, "%s: bad replay\n", argv[2]); return 2; }
    side = atoi(argv[3]);
    static Game g;
    replay_start(&rp, &g, &lv);
    for (size_t i = 0; i < rp.count; i++)
        game_tick(&g, rp.frames[i].in);
    game_mode = g.platform_count > 0;
    if (side < 0 || side >= g.player_count) { fprintf(stderr, "no player %d\n", side); return 2; }
    for (int i = 0; i < g.player_count; i++) {
        const Player *o = &g.players[i];
        if (i != side && !o->dead && !g.caught[i])
            lv.solids[lv.solid_count++] = (Solid){o->body.x, o->body.y, o->body.w, o->body.h, 1, 0};
    }
    tun = g.tuning;
    if (g.carrying[side] >= 0)
        tun.run_speed = tun.carry_speed;
    const char *t = argv[4];
    int team = t[strlen(t) - 1] - '1', fi;
    if (sscanf(t, "rest:%d,%d,%d", &rest_x0, &rest_x1, &rest_y) == 3 ||
        (sscanf(t, "ride:%d", &ride) == 1 && ride >= 0 && ride < g.platform_count)) {
        /* no touch targets */
    } else if (sscanf(t, "flag:%d", &fi) == 1 && fi >= 0 && fi < g.flag_count) {
        int *r = targets[ntargets++];
        r[0] = g.flags[fi].home_x; r[1] = g.flags[fi].home_y; r[2] = FLAG_W; r[3] = FLAG_H;
    } else if (!strncmp(t, "flags", 5)) {
        for (int i = 0; i < g.flag_count; i++)
            if (g.flags[i].team == team && ntargets < 3) {
                int *r = targets[ntargets++];
                r[0] = g.flags[i].home_x; r[1] = g.flags[i].home_y; r[2] = FLAG_W; r[3] = FLAG_H;
            }
    } else {
        int *r = targets[ntargets++];
        r[0] = g.goal_x[team]; r[1] = g.goal_y[team]; r[2] = g.goal_w[team]; r[3] = g.goal_h[team];
    }

    static const int ks[] = {3, 8, 12, 16, 20, 24, 28, 31, 34};
    nodes[0].p = g.players[side];
    nodes[0].p.push_x = 0;      /* no wind: make_replays.py follows routes in calm */
    if (game_mode)
        nodes[0].g = g;
    nodes[0].parent = -1;
    nodes[0].move[0] = 0;
    nnodes = 1;
    for (int head = 0; head < nnodes; head++) {
        const Node *cur = &nodes[head];
        for (int dir = -1; dir <= 1; dir += 2)
            for (int txs = cur->p.body.x + dir * 2; txs >= -40 && txs <= lv.px_w + 40; txs += dir * QX)
                for (int kind = K_FALL; kind <= K_DOUBLE + 8; kind++)
                    for (int air_same = 1; air_same >= 0; air_same--) {
                        int kk = kind >= K_DOUBLE ? K_DOUBLE : kind;
                        int k = kind >= K_DOUBLE ? ks[kind - K_DOUBLE] : 0;
                        Player res;
                        bool hit;
                        if (!simulate(cur, dir, txs, kk, k, air_same, &res, &hit))
                            continue;
                        int16_t ox = res.body.x, oy = res.body.y;
                        char move[48];
                        static const char *kn[] = {"fall", "jump", "drop"};
                        char kbuf[8];
                        snprintf(kbuf, sizeof kbuf, "d%d", k);
                        snprintf(move, sizeof move, "%s %d %s %s", dir < 0 ? "L" : "R", txs,
                                 kk == K_DOUBLE ? kbuf : kn[kk], air_same ? (dir < 0 ? "L" : "R") : "-");
                        if (rest_y >= 0 && !hit)
                            hit = oy == rest_y && ox >= rest_x0 && ox <= rest_x1;
                        if (ride >= 0 && !hit) {
                            const Solid *so = &sim.lv.solids[ride];
                            hit = oy + res.body.h == so->y && ox < so->x + so->w &&
                                  ox + res.body.w > so->x;
                        }
                        if (!hit && (seen(ox, oy) || nnodes >= MAX_NODES - 1))
                            continue;
                        nodes[nnodes].p = res;
                        if (game_mode)
                            nodes[nnodes].g = sim;
                        nodes[nnodes].parent = head;
                        memcpy(nodes[nnodes].move, move, sizeof move);
                        if (hit) {
                            print_path(nnodes);
                            return 0;
                        }
                        nnodes++;
                    }
    }
    printf("none\n");
    return 1;
}
