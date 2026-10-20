#include <stddef.h>

#include "player.h"
#include "player_tuning.h"

#define FIELD(name, fx) { #name, offsetof(PlayerTuning, name), fx }
const TuningField tuning_fields[] = {
    FIELD(run_speed, 1), FIELD(run_accel, 1), FIELD(run_decel, 1),
    FIELD(air_accel, 1), FIELD(air_decel, 1),
    FIELD(gravity, 1), FIELD(apex_threshold, 1), FIELD(fall_max, 1),
    FIELD(jump_speed, 1), FIELD(jump_cut, 1), FIELD(double_jump_speed, 1),
    FIELD(dash_speed, 1), FIELD(dash_decel, 1), FIELD(dash_frames, 0), FIELD(dash_cooldown, 0),
    FIELD(wall_slide_max, 1), FIELD(wall_jump_x, 1), FIELD(wall_jump_y, 1),
    FIELD(wall_jump_lock, 0), FIELD(wall_reach, 0),
    FIELD(climb_speed, 1), FIELD(pad_speed, 1),
    FIELD(coyote_frames, 0), FIELD(jump_buffer_frames, 0), FIELD(drop_frames, 0),
    FIELD(hurt_frames, 0), FIELD(invuln_frames, 0),
    FIELD(attack_frames, 0), FIELD(attack_active, 0), FIELD(attack_cooldown, 0),
    FIELD(pogo_speed, 1), FIELD(knock_x, 1), FIELD(knock_y, 1), FIELD(knock_frames, 0),
    FIELD(shield_recharge, 0),
    FIELD(carry_speed, 1), FIELD(weight_speed, 1), FIELD(weight_jump, 1),
    FIELD(fruit_min, 0), FIELD(fruit_range, 0),
    FIELD(ice_accel, 1), FIELD(ice_decel, 1),
    FIELD(belt_speed, 1),
};
const int tuning_field_count = (int)(sizeof tuning_fields / sizeof tuning_fields[0]);

void tuning_defaults(PlayerTuning *t)
{
    static const PlayerTuning defaults = PLAYER_TUNING_DEFAULTS;
    *t = defaults;
}

static fx approach(fx v, fx target, fx step)
{
    if (v < target)
        return v + step < target ? v + step : target;
    return v - step > target ? v - step : target;
}

static fx fx_abs(fx v) { return v < 0 ? -v : v; }

static int center_cell_x(const Body *b) { return (b->x + b->w / 2) >> TILE_SHIFT_X; }

/* Either foot on an ice cell. */
static int on_ice(const Body *b, const Level *lv)
{
    int cy = (b->y + b->h) >> TILE_SHIFT_Y;
    return level_surface(lv, b->x >> TILE_SHIFT_X, cy) == SURFACE_ICE ||
           level_surface(lv, (b->x + b->w - 1) >> TILE_SHIFT_X, cy) == SURFACE_ICE;
}

fx belt_push(const Body *b, const Level *lv, const PlayerTuning *t)
{
    int cy = (b->y + b->h) >> TILE_SHIFT_Y;
    int xs[3] = {b->x + b->w / 2, b->x, b->x + b->w - 1};
    for (int i = 0; i < 3; i++) {
        uint8_t s = level_surface(lv, xs[i] >> TILE_SHIFT_X, cy);
        if (s == SURFACE_BELT_L || s == SURFACE_BELT_R)
            return s == SURFACE_BELT_L ? -t->belt_speed : t->belt_speed;
    }
    return 0;
}

static int overlaps_ladder(const Body *b, const Level *lv)
{
    int cx = center_cell_x(b);
    for (int cy = b->y >> TILE_SHIFT_Y; cy <= (b->y + b->h - 1) >> TILE_SHIFT_Y; cy++)
        if (level_cell(lv, cx, cy) == CELL_LADDER)
            return 1;
    return 0;
}

static int ladder_below(const Body *b, const Level *lv)
{
    return level_cell(lv, center_cell_x(b), (b->y + b->h) >> TILE_SHIFT_Y) == CELL_LADDER;
}

static int wall_near(const Body *b, const Level *lv, int dir, int reach)
{
    for (int d = 1; d <= reach; d++)
        if (level_box_solid(lv, b->x + dir * d, b->y, b->w, b->h))
            return 1;
    return 0;
}

static int on_ground(const Player *p, const Level *lv)
{
    return body_on_solid(&p->body, lv) || (!p->drop && body_on_platform(&p->body, lv));
}

/* The air dash refills on ground, ladders and jump pads; the double jump
 * only on ground (standing on a solid or platform top). */
static void refill_dash(Player *p)
{
    p->air_dashes = (p->abilities & ABIL_DASH) ? 1 : 0;
}

static void refill(Player *p)
{
    p->air_jumps = (p->abilities & ABIL_DOUBLE_JUMP) ? 1 : 0;
    refill_dash(p);
}

void player_spawn(Player *p, int16_t x, int16_t y, uint8_t abilities, uint8_t modifiers)
{
    *p = (Player){0};
    p->body.x = x;
    p->body.y = y;
    p->body.w = PLAYER_HIT_W;
    p->body.h = PLAYER_HIT_H;
    p->facing = 1;
    p->abilities = abilities;
    p->modifiers = modifiers;
    p->shield = (modifiers & MOD_SHIELD) != 0;
    p->health = PLAYER_HEALTH;
    p->safe_x = x;
    p->safe_y = y;
    refill(p);
}

void player_place(Player *p, int16_t x, int16_t y, int keep_velocity)
{
    Body *b = &p->body;
    b->x = x;
    b->y = y;
    b->rem_x = b->rem_y = 0;
    if (!keep_velocity)
        b->vx = b->vy = 0;
    if (p->state != PS_NORMAL)
        p->state = PS_NORMAL;
    p->dash_timer = p->hurt_timer = p->knock = p->wall_lock = 0;
    p->safe_x = x;
    p->safe_y = y;
}

static void set_anim(Player *p, uint8_t anim)
{
    if (p->anim != anim) {
        p->anim = anim;
        p->anim_tick = 0;
    }
}

static void animate(Player *p, const PlayerTuning *t)
{
    const Body *b = &p->body;
    switch (p->state) {
    case PS_HURT:
        if (p->health == 0) {
            set_anim(p, ANIM_DEATH);
            p->anim_frame = (uint8_t)(p->anim_tick / 5 < 7 ? p->anim_tick / 5 : 7);
        } else {
            set_anim(p, ANIM_HURT);
            p->anim_frame = (uint8_t)((p->anim_tick / 6) % 4);
        }
        break;
    case PS_CLIMB:
        set_anim(p, ANIM_CLIMB);
        p->anim_frame = (uint8_t)((p->anim_tick / 8) % 4);
        if (b->vy == 0)
            return;                 /* hold the frame while not moving */
        break;
    case PS_DASH:
        set_anim(p, ANIM_DASH);
        p->anim_frame = (uint8_t)((p->anim_tick / 2) % 3);
        break;
    default:
        if (p->knock) {
            set_anim(p, ANIM_HURT);
            p->anim_frame = (uint8_t)((p->anim_tick / 4) % 4);
        } else if (p->attack_timer) {
            set_anim(p, ANIM_ATTACK);
            int age = t->attack_frames - p->attack_timer;
            p->anim_frame = (uint8_t)(age * 4 / (t->attack_frames ? t->attack_frames : 1));
        } else if (!p->grounded) {
            set_anim(p, ANIM_JUMP);
            p->anim_frame = b->vy < -FX(2) ? 3 : b->vy < FX(1) ? 4 : 5;
        } else if (p->land_timer) {
            set_anim(p, ANIM_JUMP);
            p->anim_frame = 7;
        } else if (b->vx != 0) {
            set_anim(p, ANIM_RUN);
            p->anim_frame = (uint8_t)((p->anim_tick / 5) % 6);
        } else {
            set_anim(p, ANIM_IDLE);
            p->anim_frame = (uint8_t)((p->anim_tick / 8) % 4);
        }
        break;
    }
    p->anim_tick++;
}

static void respawn(Player *p, const PlayerTuning *t)
{
    Body *b = &p->body;
    if (p->health == 0) {
        p->dead = 1;
        p->state = PS_NORMAL;
        return;
    }
    b->x = p->safe_x;
    b->y = p->safe_y;
    b->vx = b->vy = b->rem_x = b->rem_y = 0;
    p->state = PS_NORMAL;
    p->invuln = (uint8_t)t->invuln_frames;
    p->jumping = p->jump_buffer = p->wall_lock = p->drop = p->knock = 0;
    p->dash_cooldown = 0;
    p->attack_timer = 0;
    refill(p);
}

/* Take one hit: the shield absorbs it if charged, else lose a pip. Returns
 * 1 if a pip was lost. */
static int take_hit(Player *p, const PlayerTuning *t)
{
    p->shield_timer = (uint16_t)t->shield_recharge;   /* recharge restarts on any hit */
    if (p->shield) {
        p->shield = 0;
        p->sfx |= SFX_BIT(SFX_SHIELD);
        return 0;
    }
    if (p->health)
        p->health--;
    p->sfx |= SFX_BIT(SFX_HURT);
    return 1;
}

void player_hurt(Player *p, const PlayerTuning *t)
{
    if (p->state == PS_HURT || p->dead)
        return;
    take_hit(p, t);
    p->state = PS_HURT;
    p->hurt_timer = (uint8_t)t->hurt_frames;
    p->body.vx = p->body.vy = 0;
    p->attack_timer = 0;
}

int player_damage(Player *p, const PlayerTuning *t, int dir)
{
    if (p->invuln || p->state == PS_HURT || p->dead)
        return 0;
    if (!p->shield && p->health <= 1) {
        player_hurt(p, t);          /* last pip: death animation, then dead */
        return 1;
    }
    take_hit(p, t);
    p->state = PS_NORMAL;
    p->body.vx = dir * t->knock_x;
    p->body.vy = -t->knock_y;
    p->body.rem_x = 0;
    p->knock = (uint8_t)t->knock_frames;
    p->invuln = (uint8_t)t->invuln_frames;
    p->jumping = p->jump_buffer = 0;
    p->attack_timer = 0;
    return 1;
}

void player_pogo(Player *p, const PlayerTuning *t)
{
    p->state = PS_NORMAL;
    p->body.vy = -t->pogo_speed;
    p->jumping = 0;
    p->attack_pogo = 1;
    p->dash_cooldown = 0;
    p->sfx |= SFX_BIT(SFX_POGO);
    refill_dash(p);
}

int player_attack_box(const Player *p, const PlayerTuning *t, int *x, int *y, int *w, int *h)
{
    const Body *b = &p->body;
    if (p->attack_timer <= t->attack_frames - t->attack_active)
        return 0;
    switch (p->attack_dir) {
    case ATK_UP:
        *x = b->x - 4; *y = b->y - 10; *w = b->w + 8; *h = 12;
        break;
    case ATK_DOWN:
        *x = b->x - 4; *y = b->y + b->h - 2; *w = b->w + 8; *h = 12;
        break;
    default:
        *x = p->facing > 0 ? b->x + b->w : b->x - 16; *y = b->y + 3; *w = 16; *h = 10;
        break;
    }
    return 1;
}

void player_launch(Player *p, fx speed)
{
    if (p->state == PS_HURT)
        return;
    p->state = PS_NORMAL;
    p->body.vy = -speed;
    p->jumping = 0;
    p->coyote = p->jump_buffer = 0;
    p->dash_cooldown = 0;
    p->sfx |= SFX_BIT(SFX_PAD);
    refill_dash(p);
}

static void end_dash(Player *p, const PlayerTuning *t, int hit_wall)
{
    p->state = PS_NORMAL;
    p->body.vx = hit_wall ? 0 : p->dash_dir * t->run_speed;
    p->dash_cooldown = (uint8_t)t->dash_cooldown;
}

static void start_climb(Player *p, const Level *lv)
{
    Body *b = &p->body;
    int16_t x = (int16_t)(center_cell_x(b) * TILE_W + (TILE_W - b->w) / 2);
    if (level_box_solid(lv, x, b->y, b->w, b->h))
        return;
    b->x = x;
    b->vx = b->vy = b->rem_x = 0;
    p->state = PS_CLIMB;
    p->jumping = 0;
    refill_dash(p);
}

void player_tick(Player *p, const Level *lv, const PlayerTuning *t,
                 const InputState *in, uint16_t pressed)
{
    Body *b = &p->body;
    uint16_t held = in->buttons;

    p->sfx = 0;
    if (p->invuln)
        p->invuln--;
    if (p->attack_timer)
        p->attack_timer--;
    if (p->attack_cooldown)
        p->attack_cooldown--;
    if ((p->modifiers & MOD_SHIELD) && !p->shield && (p->shield_timer == 0 || --p->shield_timer == 0))
        p->shield = 1;
    if (p->dead)
        return;
    if (p->state == PS_HURT) {
        if (p->hurt_timer == 0 || --p->hurt_timer == 0)
            respawn(p, t);
        animate(p, t);
        return;
    }
    if (p->dash_cooldown)
        p->dash_cooldown--;
    if (p->ladder_lock)
        p->ladder_lock--;
    if (p->drop)
        p->drop--;
    if (p->land_timer)
        p->land_timer--;
    if (p->knock) {
        p->knock--;
        pressed = 0;
        held = 0;
        p->jump_buffer = 0;
    }

    int on_solid = body_on_solid(b, lv);
    int on_plat = !p->drop && body_on_platform(b, lv);
    p->grounded = (on_solid || on_plat) && b->vy >= 0 && p->state != PS_CLIMB;
    fx push = p->push_x + (p->grounded ? belt_push(b, lv, t) : 0);  /* wind, belt */
    if (p->grounded) {
        p->coyote = (uint8_t)t->coyote_frames;
        p->jumping = 0;
        if (p->state != PS_DASH)
            refill(p);
    } else if (p->coyote) {
        p->coyote--;
    }
    if (pressed & BTN_JUMP)
        p->jump_buffer = (uint8_t)t->jump_buffer_frames;
    else if (p->jump_buffer)
        p->jump_buffer--;

    int dir = ((held & BTN_RIGHT) != 0) - ((held & BTN_LEFT) != 0);
    fx target = dir * t->run_speed;
    if (in->stick_x) {
        int s = in->stick_x;
        s = s > STICK_FULL ? STICK_FULL : s < -STICK_FULL ? -STICK_FULL : s;
        target = t->run_speed * s / STICK_FULL;
        dir = s > 0 ? 1 : -1;
    }

    p->wall_dir = 0;
    if (!p->grounded && p->state != PS_CLIMB && (p->abilities & ABIL_WALL_JUMP)) {
        if (dir && wall_near(b, lv, dir, t->wall_reach))
            p->wall_dir = (int8_t)dir;
        else if (wall_near(b, lv, -1, t->wall_reach))
            p->wall_dir = -1;
        else if (wall_near(b, lv, 1, t->wall_reach))
            p->wall_dir = 1;
    }

    /* Ladders. */
    if (p->state == PS_NORMAL && !p->ladder_lock &&
        (((held & BTN_UP) && overlaps_ladder(b, lv)) ||
         ((held & BTN_DOWN) && p->grounded && ladder_below(b, lv))))
        start_climb(p, lv);
    if (p->state == PS_CLIMB) {
        if (p->jump_buffer) {
            p->state = PS_NORMAL;
            p->ladder_lock = 12;
            p->coyote = 1;          /* jump off the ladder below */
        } else if (pressed & (BTN_LEFT | BTN_RIGHT)) {
            p->state = PS_NORMAL;
            p->ladder_lock = 12;
        } else {
            int vdir = ((held & BTN_DOWN) != 0) - ((held & BTN_UP) != 0);
            b->vy = vdir * t->climb_speed;
            if (body_move_y(b, b->vy, lv, 0)) {
                b->vy = 0;
                if (vdir > 0)
                    p->state = PS_NORMAL;
            }
            if (!overlaps_ladder(b, lv)) {
                p->state = PS_NORMAL;
                b->vy = 0;
            }
            p->grounded = p->state == PS_NORMAL && on_ground(p, lv);
            animate(p, t);
            return;
        }
    }

    /* Dash. */
    if (p->state == PS_NORMAL && (pressed & BTN_DASH) && (p->abilities & ABIL_DASH) &&
        !p->dash_cooldown && (p->grounded || p->air_dashes)) {
        p->state = PS_DASH;
        p->air_dashes = 0;
        p->dash_dir = (int8_t)(dir ? dir : p->facing);
        p->facing = p->dash_dir;
        p->dash_timer = (uint8_t)t->dash_frames;
        p->jumping = 0;
        p->wall_lock = 0;
        p->attack_timer = 0;
        b->rem_x = 0;           /* exact distance: dash_frames * speed - decel terms */
        p->sfx |= SFX_BIT(SFX_DASH);
    }
    if (p->state == PS_DASH) {
        if (p->jump_buffer && (p->coyote || p->wall_dir || p->air_jumps)) {
            end_dash(p, t, 0);      /* jump out of the dash below */
        } else {
            /* Fast start, slowing by dash_decel each frame. */
            b->vy = 0;
            b->vx = p->dash_dir * (t->dash_speed - t->dash_decel * (t->dash_frames - p->dash_timer));
            int hit = body_move_x(b, b->vx + push, lv);
            if (hit || --p->dash_timer == 0)
                end_dash(p, t, hit);
            p->grounded = on_ground(p, lv);
            animate(p, t);
            return;
        }
    }

    /* Attack: up, down (airborne only) or to the facing side. */
    if (p->state == PS_NORMAL && (pressed & BTN_ATTACK) && !p->attack_cooldown) {
        p->attack_timer = (uint8_t)t->attack_frames;
        p->attack_cooldown = (uint8_t)t->attack_cooldown;
        p->attack_dir = (held & BTN_UP) ? ATK_UP
                      : ((held & BTN_DOWN) && !p->grounded) ? ATK_DOWN : ATK_SIDE;
        p->attack_id++;
        p->attack_pogo = 0;
        p->sfx |= SFX_BIT(SFX_ATTACK);
    }

    /* Jumps: ground (incl. coyote), drop-through, wall, double. */
    if (p->jump_buffer) {
        if (p->coyote && (held & BTN_DOWN) && on_plat && !on_solid) {
            p->drop = (uint8_t)t->drop_frames;
            p->jump_buffer = p->coyote = 0;
        } else if (p->coyote) {
            b->vy = -t->jump_speed;
            p->jumping = 1;
            p->jump_buffer = p->coyote = 0;
            p->sfx |= SFX_BIT(SFX_JUMP);
        } else if (p->wall_dir) {
            b->vy = -t->wall_jump_y;
            b->vx = -p->wall_dir * t->wall_jump_x;
            p->facing = (int8_t)-p->wall_dir;
            p->wall_lock = (uint8_t)t->wall_jump_lock;
            p->jumping = 1;
            p->jump_buffer = 0;
            p->sfx |= SFX_BIT(SFX_JUMP);
        } else if (p->air_jumps) {
            p->air_jumps--;
            b->vy = -t->double_jump_speed;
            p->jumping = 1;
            p->jump_buffer = 0;
            p->sfx |= SFX_BIT(SFX_DOUBLE_JUMP);
        }
    }

    /* Horizontal. */
    if (p->wall_lock) {
        p->wall_lock--;
    } else if (!p->knock) {
        int speeding_up = target != 0 && (b->vx == 0 || (b->vx > 0) == (target > 0)) &&
                          fx_abs(b->vx) <= fx_abs(target);
        fx a = !p->grounded  ? (speeding_up ? t->air_accel : t->air_decel)
               : on_ice(b, lv) ? (speeding_up ? t->ice_accel : t->ice_decel)
                               : (speeding_up ? t->run_accel : t->run_decel);
        b->vx = approach(b->vx, target, a);
        if (dir)
            p->facing = (int8_t)dir;
    }

    /* Vertical: jump cut, gravity (halved near the apex while held), wall
     * slide, terminal velocity. */
    if (p->jumping && !(held & BTN_JUMP) && b->vy < -t->jump_cut) {
        b->vy = -t->jump_cut;
        p->jumping = 0;
    }
    if (b->vy >= 0)
        p->jumping = 0;
    fx g = t->gravity;
    if ((held & BTN_JUMP) && fx_abs(b->vy) < t->apex_threshold)
        g /= 2;
    b->vy += g;
    p->wall_sliding = 0;
    if (p->wall_dir && dir == p->wall_dir && b->vy > 0) {
        p->wall_sliding = 1;
        if (b->vy > t->wall_slide_max)
            b->vy = t->wall_slide_max;
    }
    if (b->vy > t->fall_max)
        b->vy = t->fall_max;

    int was_grounded = p->grounded;
    fx fall = b->vy;
    if (body_move_x(b, b->vx + push, lv))
        b->vx = 0;
    if (body_move_y(b, b->vy, lv, !p->drop)) {
        if (b->vy > 0 && !was_grounded && fall > FX(3)) {
            p->land_timer = 4;
            p->sfx |= SFX_BIT(SFX_LAND);
        }
        b->vy = 0;
    }
    p->grounded = b->vy >= 0 && on_ground(p, lv);

    /* Safe ground: both feet on solid tiles (not moving solids), no hazard
     * within one cell. */
    if (p->grounded && body_on_solid(b, lv) &&
        level_box_has(lv, b->x, b->y + b->h, 1, 1, CELL_SOLID) &&
        level_box_has(lv, b->x + b->w - 1, b->y + b->h, 1, 1, CELL_SOLID) &&
        !level_box_has(lv, b->x - TILE_W, b->y, b->w + 2 * TILE_W, b->h + TILE_H,
                       CELL_HAZARD)) {
        p->safe_x = b->x;
        p->safe_y = b->y;
    }
    animate(p, t);
}
