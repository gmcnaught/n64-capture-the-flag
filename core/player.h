/* Player movement: run, jump, double jump, dash, wall slide/jump, ladders,
 * one-way platforms, melee attack with down-hit pogo, damage with knockback,
 * shield modifier, hazard respawn. Ported from n64-platformer; both players
 * run this code. Horizontal values are in 640-wide sim px (core/level.h). */
#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

#include "body.h"
#include "input.h"
#include "level.h"

/* All fields int32_t so the host tuning panel can edit them uniformly;
 * fields marked fx in tuning_fields[] are Q16.16. */
typedef struct {
    int32_t run_speed, run_accel, run_decel, air_accel, air_decel;
    int32_t gravity, apex_threshold, fall_max;
    int32_t jump_speed, jump_cut, double_jump_speed;
    int32_t dash_speed, dash_decel, dash_frames, dash_cooldown;
    int32_t wall_slide_max, wall_jump_x, wall_jump_y, wall_jump_lock, wall_reach;
    int32_t climb_speed, pad_speed;
    int32_t coyote_frames, jump_buffer_frames, drop_frames;
    int32_t hurt_frames, invuln_frames;
    int32_t attack_frames, attack_active, attack_cooldown, pogo_speed;
    int32_t knock_x, knock_y, knock_frames, shield_recharge;
    int32_t carry_speed;        /* run_speed while carrying a flag (core/game.c) */
    int32_t weight_speed;       /* run_speed while the other player stands on you */
    int32_t weight_jump;        /* jump and double jump speed while stood on */
    int32_t fruit_min, fruit_range; /* frames until the next water fruit: min + rnd % range */
    int32_t ice_accel, ice_decel;   /* run_accel, run_decel standing on SURFACE_ICE */
} PlayerTuning;

typedef struct {
    const char *name;
    uint16_t offset;    /* offsetof(PlayerTuning, field) */
    uint8_t is_fx;
} TuningField;

extern const TuningField tuning_fields[];
extern const int tuning_field_count;
/* The last ICE_TUNING_FIELDS entries of tuning_fields are the ice ones
 * (game_hash mixes them only on levels with ice). */
#define ICE_TUNING_FIELDS 2
void tuning_defaults(PlayerTuning *t);

enum { PS_NORMAL, PS_DASH, PS_CLIMB, PS_HURT };

enum { ATK_SIDE, ATK_UP, ATK_DOWN };

/* Defensive modifiers (game design §4.1); saved, part of the hash. */
enum { MOD_SHIELD = 1 << 0 };

/* Sound events raised during a tick (Player.sfx, Game.sfx). */
enum {
    SFX_JUMP, SFX_DOUBLE_JUMP, SFX_DASH, SFX_LAND, SFX_ATTACK, SFX_HIT, SFX_KILL,
    SFX_HURT, SFX_POGO, SFX_PAD, SFX_PICKUP, SFX_SAVE, SFX_SHIELD, SFX_MENU, SFX_ROOM,
    SFX_WIND, SFX_COUNT
};
#define SFX_BIT(s) (1u << (s))

enum {
    ABIL_DOUBLE_JUMP = 1 << 0,
    ABIL_DASH        = 1 << 1,
    ABIL_WALL_JUMP   = 1 << 2,
    ABIL_ALL         = 0x07,
};

/* Animations: rows of the character sheets (tools/build_sprites.py). */
enum { ANIM_IDLE, ANIM_RUN, ANIM_JUMP, ANIM_CLIMB, ANIM_DEATH, ANIM_HURT, ANIM_ATTACK, ANIM_DASH,
       ANIM_COUNT };

/* Dino Characters: 12x16 art px body in a 24x24 art px frame, art pixels
 * 2 sim px wide. In the unflipped (right-facing) frame the body is centered
 * at x PLAYER_SPRITE_CX and its feet are on row PLAYER_SPRITE_FOOT. */
#define PLAYER_HIT_W 24
#define PLAYER_HIT_H 16
#define PLAYER_SPRITE_W 48
#define PLAYER_SPRITE_H 24
#define PLAYER_SPRITE_CX 27
#define PLAYER_SPRITE_FOOT 21
#define PLAYER_HEALTH 3

typedef struct {
    Body body;
    uint8_t state;
    int8_t facing;              /* -1 left, +1 right */
    uint8_t abilities;
    uint8_t health;
    uint8_t grounded;           /* standing on solid or platform this frame */
    int8_t wall_dir;            /* wall within reach while airborne, or 0 */
    uint8_t wall_sliding;
    uint8_t air_jumps, air_dashes;
    uint8_t coyote, jump_buffer;
    uint8_t jumping;            /* rising from a jump that can still be cut */
    uint8_t wall_lock;          /* frames of ignored horizontal input */
    uint8_t drop;               /* frames of ignoring platforms */
    uint8_t ladder_lock;        /* frames before a ladder can be grabbed */
    int8_t dash_dir;
    uint8_t dash_timer, dash_cooldown;
    uint8_t hurt_timer, invuln;
    uint8_t land_timer;
    uint8_t attack_timer;       /* counts down from attack_frames */
    uint8_t attack_dir;         /* ATK_* */
    uint8_t attack_cooldown;
    uint8_t attack_id;          /* increments per swing; targets are hit once per swing */
    uint8_t attack_pogo;        /* this swing already bounced */
    uint8_t knock;              /* frames of knockback (no control) */
    uint8_t modifiers;          /* MOD_* */
    uint8_t shield;             /* shield charged */
    uint16_t shield_timer;      /* frames until the shield recharges */
    uint8_t dead;               /* health ran out; the game respawns at the save point */
    fx push_x;                  /* external horizontal push this frame (wind), set by the game */
    int16_t safe_x, safe_y;     /* last safe ground (hazard respawn) */
    uint8_t anim, anim_frame;
    uint16_t anim_tick;
    uint32_t sfx;               /* SFX_BIT()s raised this tick */
} Player;

void player_spawn(Player *p, int16_t x, int16_t y, uint8_t abilities, uint8_t modifiers);
void player_tick(Player *p, const Level *lv, const PlayerTuning *t,
                 const InputState *in, uint16_t pressed);
/* Hazard hit: lose a pip (or the shield), freeze, then respawn at safe
 * ground. With no pips left, sets dead instead of respawning. */
void player_hurt(Player *p, const PlayerTuning *t);
/* Enemy hit from side dir (-1: pushed left, +1: pushed right): lose a pip
 * (or the shield) and get knocked back. Ignored while invulnerable. Returns
 * 1 if the hit landed. The last pip goes through player_hurt (death). */
int player_damage(Player *p, const PlayerTuning *t, int dir);
/* Launch upward (jump pad); refreshes the air dash (not the double jump). */
void player_launch(Player *p, fx speed);
/* Down-hit bounce; refreshes the air dash (not the double jump). */
void player_pogo(Player *p, const PlayerTuning *t);
/* Hitbox of the active attack, px. Returns 0 when no hitbox is active. */
int player_attack_box(const Player *p, const PlayerTuning *t, int *x, int *y, int *w, int *h);
/* Place at (x, y) with velocity kept or cleared, ending any special state. */
void player_place(Player *p, int16_t x, int16_t y, int keep_velocity);

#endif
