/* Game state: one arena, two teams of one or two players, one fixed tick
 * per displayed frame.
 *
 * The whole arena is on screen (640x240 sim px, core/level.h), so there is
 * no camera. Capture the flag:
 *   - each team has FLAGS_PER_TEAM flags at home (its start); a flag at
 *     home can only be taken by the other team;
 *   - a flag away from home (dropped, or lying in a goal) can be picked up
 *     by either player; one flag at a time, by touching it;
 *   - a carrier runs at tuning.carry_speed and cannot dash or attack; carrying an enemy flag into the
 *     own goal places it there, carrying an own flag to the home area
 *     returns it home;
 *   - a hit (attack or hazard) on a carrier drops the flag where it is (it
 *     falls) and sends the carrier back to its start, without losing a pip;
 *   - a team wins the round with all of the other team's flags in its goal;
 *     after ROUND_END_FRAMES a new round starts.
 *   - every round opens with a 3, 2, 1 countdown (COUNTDOWN_STEP frames
 *     each) with everything frozen; play starts on GO!, which stays on
 *     screen for COUNTDOWN_GO frames.
 * Players: Game.player_count, 2 to MAX_PLAYERS. Player i is on team
 * i % TEAM_COUNT: P1 and P3 red, P2 and P4 blue; P3 starts by P1 and P4
 * by P2, TEAMMATE_GAP apart, on the side away from the middle. Teammates cannot hit
 * each other.
 * Players are solid to each other: they block, a head-bump stops a jump,
 * and one can stand on another (in a stack). A player underneath is weighed
 * down (tuning.weight_speed, weight_jump) and lifts its riders when it
 * jumps; a rider landing on a rising player stops that jump. Players that
 * overlap (after a respawn) pass through each other until they separate.
 * Water fruit (Bubble Bobble water): every tuning.fruit_min .. fruit_min +
 * fruit_range frames (15..25 s by default) a fruit appears at a random FruitSpot (sim PRNG) for FRUIT_LIFE
 * frames. Touching it releases a stream of WATER_LEN cells at the spot,
 * flowing the way the toucher faces: each WATER_STEP frames the front falls
 * if there is no floor below (one-way slabs are floors), else moves
 * sideways, turning at walls. Past a platform edge it runs on up to
 * WATER_OVERHANG cells when falling from there lands on a higher platform
 * than falling straight down (so it steps from platform to platform). It drains on reaching a wall on the bottom
 * ground, leaving the arena, or after WATER_MAX_STEPS. A player touching
 * the water (the releaser only after WATER_GRACE frames) is caught: carried
 * at the front without control, put down standing where it drains. A
 * carrier keeps its flag.
 * Moving platforms carry, push and squish players and dropped flags
 * (Platform, below).
 * Hits on a player without a flag knock back and cost a pip; at 0 pips the
 * player plays the death animation and respawns at its start after
 * RESPAWN_FRAMES. Hazards cost a pip and respawn at the last safe ground.
 * Screens: the title screen (game_title) waits for Start or Jump from any
 * controller, then shows the setup screen: Up/Down pick a row and
 * Left/Right change it: STAGE (the level, Game.stage; the platform sees
 * the change, loads that level and calls game_set_level) and PLAYERS
 * (Game.player_count); Start starts a match (rounds won and knockouts
 * reset), Attack goes back to the title. The picks hold for the match;
 * the backdrop is the level's.
 * Any player's Start pauses and resumes; paused, Up/Down move the menu
 * cursor and Jump/Attack picks RESUME, CONTROLS (a controls screen; Start,
 * Jump or Attack goes back to the menu), RESTART (new match) or QUIT
 * (title screen). The screens live in the sim so replays reproduce them; replays
 * start in play (game_init), the platforms call game_title.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#include "input.h"
#include "level.h"
#include "player.h"

#define VIEW_W 640
#define VIEW_H 240
#define TEAM_COUNT 2
#define MAX_PLAYERS 4
#define TEAMMATE_GAP 12         /* px between P1 and P3 (P2 and P4) at the start: the sprites
                                 * reach 3 px past the 24 px body on each side */
#define MAX_PADS 16
#define PAD_ANIM_FRAMES 20      /* JumpPad.timer after a launch */
#define RESPAWN_FRAMES 90       /* dead -> respawn at the team start */
/* Launch area of a pad, relative to its cell: the resting pad's top is
 * PAD_HIT_Y px down the cell (scene.c draws a 16x16 frame on the cell). */
#define PAD_HIT_X 2
#define PAD_HIT_Y 3
#define FLAGS_PER_TEAM 3
#define MAX_GAME_FLAGS (TEAM_COUNT * FLAGS_PER_TEAM)
#define FLAG_W 16               /* flag body: pole and cloth */
#define FLAG_H 16
#define ROUND_END_FRAMES 180
#define COUNTDOWN_STEP 60       /* frames per number of the round countdown */
#define COUNTDOWN_GO 40         /* frames GO! shows, in play */
#define COUNTDOWN_FRAMES (3 * COUNTDOWN_STEP + COUNTDOWN_GO)
#define MAX_FRUIT_SPOTS 8
/* Wind (ENT_WIND zones): each cycle of a zone's `cycle` frames, counted
 * from the end of the round countdown plus its phase, is calm, then
 * WIND_WARN frames of warning (render and sound only), then a gust of
 * `gust` frames ramping up and down over WIND_RAMP frames. Bodies in the
 * zone are pushed by its force: sideways, or for a geyser (`up`) upward,
 * the force added to their rising speed each frame up to WIND_LIFT_MAX. */
#define WIND_WARN 60
#define WIND_RAMP 20
#define MAX_WINDS 4
#define WIND_LIFT_MAX FX(5)     /* fastest rise in a geyser, px/frame */

typedef struct {
    int16_t x, y, w, h;
    int32_t peak;                   /* fx px/frame, sign = first gust direction */
    uint16_t cycle, gust, phase;
    uint8_t alternate;              /* each gust the other way */
    uint8_t dust;                   /* drawn as a dust storm (else streaks; render only) */
    uint8_t up;                     /* a geyser: force pushes up (px/frame^2) */
    int32_t force;                  /* fx px/frame pushing right (negative: left) this frame;
                                     * a geyser: upward, px/frame^2 */
    int8_t dir;                     /* direction of the current or next gust: 1 right, -1 left */
} WindZone;
/* Moving platforms (ENT_PLATFORM): a solid box (Level.solids, the same
 * index) going from (x0, y0) to (x1, y1) and back at speed, standing
 * PLATFORM_WAIT frames at each end, reset at each round start to phase
 * frames into that cycle. Each tick, before the players move, it moves
 * by whole px: players and dropped flags on its top (a player standing on
 * a rider, too) go with it, players in its way are pushed, and one pushed
 * into a wall is squished: back to its start without losing a pip (a
 * carrier drops its flag first). Dropped flags are not pushed (one under
 * a platform stays there). A one-way platform carries only; rising, it
 * also picks up a body whose feet it passes. */
#define MAX_PLATFORMS (LEVEL_MAX_SOLIDS - MAX_PLAYERS + 1)  /* then the other players */
#define PLATFORM_WAIT 30

typedef struct {
    int16_t x0, y0, x1, y1;         /* path ends, top-left sim px */
    int32_t speed;                  /* fx px/frame along the path's longer axis */
    uint16_t phase;                 /* frames into the cycle at a round start */
    int32_t prog;                   /* fx px along the longer axis from (x0, y0) */
    int8_t dir;                     /* 1 toward (x1, y1), -1 back */
    uint8_t wait;                   /* frames left standing at an end */
} Platform;

#define FRUIT_LIFE 600          /* an untouched fruit disappears after 10 s */
#define WATER_LEN 12            /* stream length, cells */
#define WATER_STEP 3            /* frames per cell the front moves */
#define WATER_MAX_STEPS 120
#define WATER_GRACE 30          /* frames the releaser is not caught */
#define WATER_OVERHANG 3        /* cells the stream may run on past an edge */

enum { FS_HOME, FS_CARRIED, FS_DROPPED, FS_PLACED };
enum { MENU_RESUME, MENU_CONTROLS, MENU_RESTART, MENU_QUIT, MENU_COUNT };   /* pause menu items */
enum { PAUSE_MENU = 1, PAUSE_CONTROLS };    /* Game.paused, when nonzero */
enum { TITLE_SCREEN = 1, TITLE_SETUP };    /* Game.title, when nonzero */
enum { SETUP_STAGE, SETUP_PLAYERS, SETUP_ROWS };    /* setup screen rows */

typedef struct {
    Body body;                  /* position; falls while dropped */
    uint8_t team;               /* owner, 0 or 1 */
    uint8_t state;              /* FS_* */
    uint8_t holder;             /* FS_CARRIED: carrying player; FS_PLACED: goal's team */
    int16_t home_x, home_y;
} Flag;

typedef struct {
    int16_t x, y;               /* cell, top-left */
    uint8_t timer;              /* counts down from PAD_ANIM_FRAMES */
} JumpPad;

typedef struct {
    Level lv;
    PlayerTuning tuning;
    Player players[MAX_PLAYERS];    /* the first player_count play */
    uint8_t player_count;
    int16_t spawn_x[MAX_PLAYERS], spawn_y[MAX_PLAYERS]; /* body top-left at the player's start */
    Flag flags[MAX_GAME_FLAGS];
    uint8_t flag_count;
    int8_t carrying[MAX_PLAYERS];   /* index into flags, or -1 */
    /* Goal zones and home areas (around the team's home flags), sim px. */
    int16_t goal_x[TEAM_COUNT], goal_y[TEAM_COUNT], goal_w[TEAM_COUNT], goal_h[TEAM_COUNT];
    int16_t home_x0[TEAM_COUNT], home_y0[TEAM_COUNT], home_x1[TEAM_COUNT], home_y1[TEAM_COUNT];
    uint8_t winner;                 /* 0, or team + 1 while the round-end banner shows */
    uint16_t round_timer;           /* frames left of the banner */
    uint8_t countdown;              /* frames left of the round countdown: frozen above COUNTDOWN_GO */
    uint16_t wins[TEAM_COUNT];
    uint8_t last_hit[MAX_PLAYERS][MAX_PLAYERS]; /* [player][opponent]: the opponent's
                                                 * attack_id that last hit the player */
    uint16_t respawn[MAX_PLAYERS];  /* frames until a dead player respawns */
    uint16_t kos[MAX_PLAYERS];      /* knockouts scored by each player */
    JumpPad pads[MAX_PADS];
    uint8_t pad_count;
    /* Water fruit and its stream. */
    uint32_t rng;                   /* xorshift32 sim PRNG, fixed seed per game */
    int16_t fruit_spot_x[MAX_FRUIT_SPOTS], fruit_spot_y[MAX_FRUIT_SPOTS];  /* cell top-left px */
    uint8_t fruit_spot_count;
    uint8_t fruit_active, fruit_spot;
    uint16_t fruit_timer;           /* frames until a fruit appears, or until it goes */
    uint8_t water_len;              /* segments in the stream; 0 = no water */
    int16_t water_cx[WATER_LEN], water_cy[WATER_LEN];   /* cells, [0] = front */
    int8_t water_dir;
    uint8_t water_draining;
    uint8_t water_air;              /* the front left a floor: edge overhang decided */
    uint8_t water_extend;           /* cells left to run on past the edge */
    uint8_t water_step_timer;
    uint16_t water_steps;
    uint8_t water_owner, water_grace;   /* water_owner: player index */
    uint8_t caught[MAX_PLAYERS];    /* carried by the water */
    uint8_t abilities;
    uint16_t prev_buttons[MAX_PLAYERS];
    uint32_t frame;
    uint32_t anim;                  /* frames not paused: clock for scenery and object
                                     * animation, which stops while paused (render
                                     * only, not hashed) */
    uint8_t title;                  /* 0 (play), TITLE_SCREEN or TITLE_SETUP: the sim waits */
    uint8_t paused;                 /* 0, PAUSE_MENU or PAUSE_CONTROLS */
    uint8_t menu_sel;               /* pause menu cursor, MENU_* */
    uint8_t title_sel;              /* setup screen row, SETUP_* */
    uint8_t stage, stage_count;     /* level picked (index in the platform's level list), of how many;
                                       game_init sets 0 of 1, the platform sets both */
    uint8_t backdrop;               /* BACKDROP_*: the level's */
    /* Wind gusts (ENT_WIND); hashed only when the level has wind. */
    WindZone winds[MAX_WINDS];
    uint8_t wind_count;
    uint32_t round_frame;           /* frames of play this round (after the countdown) */
    /* Moving platforms (ENT_PLATFORM); their boxes are lv.solids[0 ..
     * platform_count - 1]. Hashed only when the level has platforms. */
    Platform platforms[MAX_PLATFORMS];
    uint8_t platform_count;
    /* Output for the platform, rewritten every tick (not hashed). */
    uint32_t sfx;               /* SFX_BIT()s raised this tick */
} Game;

/* New round in a level (kept by pointer contents; its data must outlive
 * the game). tuning may be NULL for the defaults in player_tuning.h. */
void game_init(Game *g, const Level *lv, const PlayerTuning *tuning, uint8_t abilities);

/* Show the title screen; Start or Jump goes on to the setup screen. */
void game_title(Game *g);

/* Play with n players (2 .. MAX_PLAYERS) from a new round, as the setup
 * screen does (replays with more than 2 players). */
void game_set_players(Game *g, int n);

/* Switch to another level (lv as for game_init) after Game.stage changed,
 * keeping the screen, the setup screen's picks, the tuning and the frame
 * counter; the backdrop becomes the level's. */
void game_set_level(Game *g, const Level *lv);

/* in[i] = player i + 1 (P1 .. P4; inputs of players not playing are read
 * only on the title and setup screens). */
void game_tick(Game *g, const InputState in[MAX_PLAYERS]);

/* Team of player i. */
static inline int game_team(int i) { return i % TEAM_COUNT; }

/* Frames until zone z's next gust starts: 0 while one blows. During the
 * last WIND_WARN frames the gust is signaled. */
uint16_t game_zone_calm(const Game *g, const WindZone *z);
/* The least of game_zone_calm over all zones; 0xffff without wind. */
uint16_t game_wind_calm(const Game *g);
/* Sum of the forces of the sideways zones a body is in (fx px/frame). */
int32_t game_wind_at(const Game *g, const Body *b);

/* Hash of all simulation state, for replay tests. */
uint32_t game_hash(const Game *g);

/* Attack hitbox of a player, for renderers (0 if none). */
int game_attack_box(const Game *g, const Player *p, int *x, int *y, int *w, int *h);

#endif
