/* Route finder: breadth-first search over standing positions in a level,
 * with the real core movement, for scripting replays and checking that a
 * level can be won (tools/make_replays.py).
 *
 * usage: routefind LEVEL REPLAY SIDE TARGET
 *   REPLAY  replay so far: played through game_tick, so the search starts
 *           from player SIDE's exact state (at rest), carrying or not, with
 *           the replay's tuning
 *   SIDE    0 = P1, 1 = P2
 *   TARGET  flags1 | flags2 (touch a home flag of team 1/2),
 *           goal1 | goal2 (touch the goal zone of team 1/2),
 *           flag:I (touch flag I at home; Game.flags order),
 *           rest:X0,X1,Y (come to rest with X0 <= x <= X1 at y = Y)
 * A carrier moves at tuning.carry_speed. Wind is left out (routes are
 * followed between gusts). The other player is a fixed solid
 * box where it stands at the end of the replay (it must keep still while
 * the route is followed), as the game makes it solid to the moving one.
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
    int parent;
    char move[48];
} Node;

static Level lv;
static PlayerTuning tun;
static int targets[3][4], ntargets;     /* target rectangles: home flags or a goal */
static int rest_x0, rest_x1, rest_y = -1;
static Node nodes[MAX_NODES];
static int nnodes;

static int touches(const Body *b)
{
    for (int i = 0; i < ntargets; i++) {
        int *r = targets[i];
        if (b->x < r[0] + r[2] && b->x + b->w > r[0] && b->y < r[1] + r[3] && b->y + b->h > r[1])
            return 1;
    }
    return 0;
}

static uint16_t prev;

static void tick(Player *p, uint16_t held)
{
    InputState in = {(uint16_t)(held | BTN_CONNECTED), 0, 0};
    uint16_t pressed = held & (uint16_t)~prev;
    prev = held;
    player_tick(p, &lv, &tun, &in, pressed);
}

enum { K_FALL, K_JUMP, K_DROP, K_DOUBLE };

/* Simulate one move from rest in state *from. Returns 1 if it ends at rest
 * (state in *out) or touches the target (*hit = 1), 0 if it fails. */
static int simulate(const Player *from, int dir, int txs, int kind, int k, int air_same,
                    Player *out, int *hit)
{
    Player p = *from;
    prev = 0;
    *hit = 0;
    uint16_t d = dir < 0 ? BTN_LEFT : BTN_RIGHT;
    uint16_t air = air_same ? d : 0;
    int f = 0;
    tick(&p, 0);
    if (!p.grounded)
        return 0;
    while (dir < 0 ? p.body.x > txs : p.body.x < txs) {
        tick(&p, d);
        if (touches(&p.body)) { *hit = 1; goto done; }
        if (++f > 300 || p.body.y >= lv.px_h)
            return 0;
        if (p.body.vx == 0 && p.grounded && f > 4)
            return 0;               /* blocked by a wall */
    }
    if (kind == K_DROP) {
        if (!p.grounded)
            return 0;
        tick(&p, BTN_DOWN); tick(&p, BTN_DOWN);
        tick(&p, BTN_DOWN | BTN_JUMP); tick(&p, BTN_DOWN | BTN_JUMP);
    }
    for (int j = 0; j < 400; j++) {
        uint16_t h = air;
        if (kind == K_JUMP || (kind == K_DOUBLE && (j < k - 1 || j >= k)))
            h |= BTN_JUMP;
        tick(&p, h);
        if (touches(&p.body)) { *hit = 1; goto done; }
        if (p.body.y >= lv.px_h)
            return 0;
        if (p.grounded && j > 1)
            break;
    }
    if (!p.grounded)
        return 0;
    for (int j = 0; j < 60 && (p.body.vx || !p.grounded); j++) {
        tick(&p, 0);
        if (touches(&p.body)) { *hit = 1; goto done; }
    }
done:
    *out = p;
    return 1;
}

static int seen(int x, int y)
{
    for (int i = 0; i < nnodes; i++)
        if (nodes[i].p.body.y == y && abs(nodes[i].p.body.x - x) < QX)
            return 1;
    return 0;
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
    int side = atoi(argv[3]) ? 1 : 0;
    static Game g;
    replay_start(&rp, &g, &lv);
    for (size_t i = 0; i < rp.count; i++)
        game_tick(&g, rp.frames[i].in);
    const Player *o = &g.players[!side];
    if (!o->dead && !g.caught[!side]) {
        lv.solid_x = o->body.x;
        lv.solid_y = o->body.y;
        lv.solid_w = o->body.w;
        lv.solid_h = o->body.h;
    }
    tun = g.tuning;
    if (g.carrying[side] >= 0)
        tun.run_speed = tun.carry_speed;
    const char *t = argv[4];
    int team = t[strlen(t) - 1] - '1', fi;
    if (sscanf(t, "rest:%d,%d,%d", &rest_x0, &rest_x1, &rest_y) == 3) {
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
    nodes[0].parent = -1;
    nodes[0].move[0] = 0;
    nnodes = 1;
    for (int head = 0; head < nnodes; head++) {
        const Player *cur = &nodes[head].p;
        for (int dir = -1; dir <= 1; dir += 2)
            for (int txs = cur->body.x + dir * 2; txs >= -40 && txs <= lv.px_w + 40; txs += dir * QX)
                for (int kind = K_FALL; kind <= K_DOUBLE + 8; kind++)
                    for (int air_same = 1; air_same >= 0; air_same--) {
                        int kk = kind >= K_DOUBLE ? K_DOUBLE : kind;
                        int k = kind >= K_DOUBLE ? ks[kind - K_DOUBLE] : 0;
                        Player res;
                        int hit;
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
                        if (!hit && (seen(ox, oy) || nnodes >= MAX_NODES - 1))
                            continue;
                        nodes[nnodes].p = res;
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
