/* CPU players.
 *
 * Navigation graph (BotNav), built from a level's collision cells:
 *   - nodes: standing spots, one per floor cell column where the 24x16
 *     body fits on top (solid or one-way cell); the body's top-left is
 *     (node_x, node_y);
 *   - edges: walking to the next node along the same row (when the floor
 *     between is continuous), and moves found by running the real player
 *     physics (player_tick, no other players, platforms or wind) from rest
 *     at each node: jumps (run-up, jump hold, double jump, air direction),
 *     walking off an edge and dropping through a one-way slab. A move is an
 *     edge to the node it lands on; a move that touches a hazard or falls
 *     out fails. Each node keeps the quickest move to each node it reaches,
 *     and the quickest move through each goal zone (a carrier scores on
 *     touching it, in the air too: such a move may land anywhere).
 *   - moving platforms (ENT_PLATFORM): a node on top of each at each end
 *     of its path (it waits PLATFORM_WAIT frames there); moves from it are
 *     found with the platform standing at that end, moves onto it from the
 *     nodes within PLAT_REACH px likewise (only landings on it count), and
 *     a ride edge goes to the node at the other end. The bot times them
 *     (bot_input): it jumps onto a platform when the platform will be at
 *     that end on landing, leaves one while it still waits at the end, and
 *     stands still on it while it travels;
 *   - two graphs: on foot, and carrying a flag (tuning.carry_speed).
 * The build runs bot_nav_step a few nodes at a time (bot_nav_begin starts
 * it), so a platform can spread it over frames; bot_nav_ready says when it
 * is done.
 *
 * A CPU player (Bot) is outside the simulation: each frame the platform
 * asks bot_input for the InputState of a CPU slot and passes it to
 * game_tick in place of a controller, so replays record the CPU's input
 * like a player's and play back without the bot. The bot reads the Game
 * and plans with A* over the graph (edge costs in frames; a move that
 * failed recently costs more), toward one of four aims, picked every
 * BOT_THINK frames:
 *   CAPTURE  take an enemy flag (at home or dropped) to the own goal;
 *   RECOVER  take an own flag lying in the enemy goal or dropped back home;
 *   HUNT     go after an enemy carrying an own flag and kick it;
 *   DEFEND   stay by the own flags and kick enemies that come for them.
 * To be beatable it reacts late (BOT_REACT frames before a swing), swings
 * from a slowly refilling budget of BOT_SWINGS, gives up a hunt after
 * BOT_HUNT_FRAMES, never dashes, and botches some jumps on purpose: a jump
 * whose landing is in its goal fails BOT_GOAL_MISS percent of the time,
 * any other jump BOT_JUMP_MISS percent. A player standing or coming the
 * other way on the ground ahead (up to BOT_LOOK px, before the move
 * leaves the ground) is seen; after BOT_LOOK_REACT frames and a random
 * few more the bot goes round it (that way costs more, it plans again),
 * jumps over it or waits for it. A bot walking into a player anyway (not
 * moving for a random 10 to 30 frames, 10 to 25 in a move) jumps on
 * that way, backs off or waits and goes another way. Its own PRNG
 * (Bot.rng) makes those choices.
 */
#ifndef BOT_H
#define BOT_H

#include <stdint.h>

#include "game.h"

#define NAV_MAX_NODES 512
#define NAV_MAX_EDGES 4096          /* per graph */
#define NAV_GRAPHS 2                /* on foot, carrying */
#define BOT_PATH_MAX 48

typedef enum : uint8_t { NAV_WALK, NAV_JUMP, NAV_FALL, NAV_DROP, NAV_RIDE } NavKind;

/* A move from rest at a node; the buttons of each frame come from
 * bot_move_buttons. */
typedef struct {
    uint16_t to;                    /* node it lands on */
    uint16_t cost;                  /* frames */
    NavKind kind;
    int8_t dir;                     /* held from the start (-1, 0, 1) */
    int8_t air;                     /* held after the run-up */
    uint8_t run;                    /* frames of dir before the jump (fall: before air;
                                     * a jump straight up, dir 0: frames before air) */
    uint8_t hold;                   /* frames the jump is held (0xff: to the landing) */
    uint8_t dbl;                    /* frames after the jump to the double jump; 0 none */
    uint8_t touch;                  /* bit t: touches team t's goal zone on the way */
    uint8_t touch_cost;             /* frames until it first does */
} NavEdge;

typedef struct {
    uint16_t first[NAV_MAX_NODES + 1];  /* node i's edges: [first[i], first[i + 1]) */
    uint16_t edge_count;
    NavEdge edges[NAV_MAX_EDGES];
} NavGraph;

typedef struct {
    Level lv;                       /* the level, without moving solids */
    PlayerTuning tuning;
    uint16_t node_count;            /* at most NAV_MAX_NODES - 1 (A* uses one more) */
    int16_t node_x[NAV_MAX_NODES], node_y[NAV_MAX_NODES];
    int8_t node_plat[NAV_MAX_NODES];    /* standing on this moving platform at end node_end, or -1 */
    uint8_t node_end[NAV_MAX_NODES];    /* 0: at (x0, y0), 1: at (x1, y1) */
    int16_t goal_x[TEAM_COUNT], goal_y[TEAM_COUNT], goal_w[TEAM_COUNT], goal_h[TEAM_COUNT];
    /* Moving platforms (build only): box at each end of the path. */
    uint8_t plat_count;
    int16_t plat_x[MAX_PLATFORMS][2], plat_y[MAX_PLATFORMS][2], plat_w[MAX_PLATFORMS], plat_h[MAX_PLATFORMS];
    bool plat_oneway[MAX_PLATFORMS];
    int32_t plat_speed[MAX_PLATFORMS];
    NavGraph graphs[NAV_GRAPHS];
    uint8_t graph;                  /* build progress: graph being built */
    bool done;                      /* build progress: all done */
    uint16_t next;                  /* build progress: next node of that graph */
} BotNav;

/* Start building the graph of a level (copied, without its moving solids;
 * its data must outlive the graph), with the game's tuning. */
void bot_nav_begin(BotNav *nav, const Level *lv, const PlayerTuning *t);
/* Build the moves of up to n more nodes; returns true once the graph is done. */
bool bot_nav_step(BotNav *nav, int n);
static inline bool bot_nav_ready(const BotNav *nav) { return nav->done; }
/* Buttons of move e at frame f from its start (0xffff: past its script). */
uint16_t bot_move_buttons(const NavEdge *e, int f);

/* Graph file (.nav, written by tools/navbuild.c beside each .lvl; big-endian):
 *   char  magic[4] = "NAV1"
 *   u32   key                        hash of the level's cells and the tuning
 *   u16   node_count, edge_count[NAV_GRAPHS]
 *   i16   goal x, y, w, h per team
 *   node_count times: i16 x, y; i8 plat; u8 end
 *   per graph: u16 first[node_count + 1], then edge_count times
 *              u16 to, cost; u8 kind; i8 dir, air; u8 run, hold, dbl, touch,
 *              touch_cost
 */
#define NAV_HEADER_BYTES (8 + 2 + 2 * NAV_GRAPHS + 8 * TEAM_COUNT)
#define NAV_EDGE_BYTES 12
uint32_t bot_nav_size(const BotNav *nav);
void bot_nav_write(const BotNav *nav, uint8_t *out);     /* bot_nav_size bytes */
/* Load a graph file for level lv and tuning t (the file must match both).
 * Returns 0 on success; nav is then ready. */
[[nodiscard]] int bot_nav_read(BotNav *nav, const Level *lv, const PlayerTuning *t,
                               const uint8_t *data, uint32_t size);

typedef enum : uint8_t { BOT_CAPTURE, BOT_RECOVER, BOT_HUNT, BOT_DEFEND, BOT_AIMS } BotAim;

#define BOT_THINK 20                /* frames between picks of the aim */
#define BOT_REACT 14                /* frames an enemy is in reach before a swing */
#define BOT_SWINGS 2                /* swing budget; one more every BOT_SWING_FRAMES */
#define BOT_SWING_FRAMES 120
#define BOT_HUNT_FRAMES 420         /* a hunt gives up after this, then rests */
#define BOT_HUNT_REST 300
#define BOT_GOAL_MISS 35            /* percent of jumps into the goal botched */
#define BOT_JUMP_MISS 5             /* percent of other jumps botched */
#define BOT_FIGHT 30                /* percent of chances to kick a non-carrier taken */
#define BOT_LOOK 64                 /* px ahead a player in the way is seen */
#define BOT_LOOK_REACT 4            /* frames it is seen before going round: this + 0..16 */

typedef struct {
    uint32_t rng;
    uint8_t player;
    BotAim aim;
    int8_t aim_target;              /* flag or player index */
    uint8_t think;                  /* frames to the next pick */
    /* Path: nodes and the edges between them (path_edge[i]: path_node[i] ->
     * path_node[i + 1]), index of the next edge. */
    int16_t path_node[BOT_PATH_MAX];
    int16_t path_edge[BOT_PATH_MAX];
    uint8_t path_len, path_pos, path_graph;
    int16_t goal_x, goal_y, goal_w, goal_h;     /* target box of the path */
    int16_t plan_x, plan_y;         /* goal box corner when planned */
    int8_t goal_touch;              /* the box is this team's goal zone (moves through it count), or -1 */
    bool replan;
    bool was_carrying;
    /* Move being played: edge (in path_graph), frame, left the ground. */
    int16_t move;
    NavEdge move_edge;              /* the move as played (botched: changed) */
    uint8_t move_f;
    bool move_air, move_landed;
    bool botched;                   /* this move was botched on purpose */
    uint8_t settle;                 /* frames standing still before a move */
    uint16_t wait;                  /* frames waiting for a moving platform */
    /* Combat. */
    uint8_t swings;
    uint16_t swing_timer;
    uint8_t react, react_need;      /* frames an enemy has been in reach; needed to swing */
    uint16_t hunt_timer, hunt_rest;
    bool fight_roll;                /* this enemy contact: kick a non-carrier */
    int8_t last_contact;
    /* A player in the way: frames seen; needed before going round. */
    uint8_t look, look_need;
    /* Stuck: frames without moving while trying to (in a move: blocked);
     * needed before getting out. */
    uint16_t stuck, stuck_dir;      /* stuck_dir: the button it walked with */
    uint8_t stuck_need, blocked, block_need;
    int16_t last_x, last_y;
    uint8_t wiggle;                 /* frames of a random escape move */
    uint16_t wiggle_buttons;
    uint16_t held;                  /* buttons last frame */
    /* Edge penalties: recently failed moves cost more. */
    int16_t fail_edge[8];
    uint8_t fail_graph[8], fail_age[8];
} Bot;

void bot_init(Bot *b, int player, uint32_t seed);
/* Input of CPU player b->player for this frame of g (call before
 * game_tick). Idle until the graph is ready. */
InputState bot_input(Bot *b, const BotNav *nav, const Game *g);

#endif
