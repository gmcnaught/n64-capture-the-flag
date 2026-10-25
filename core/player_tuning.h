/* Player movement tuning. Q16.16 values are px/frame (speeds) or
 * px/frame^2 (accelerations) at 60 Hz; the comment shows the decimal value.
 * The host build rewrites this file from its tuning panel (F5). */
#ifndef PLAYER_TUNING_H
#define PLAYER_TUNING_H

#define PLAYER_TUNING_DEFAULTS { \
    .run_speed          = 0x0002b333, /* 2.7000 */ \
    .run_accel          = 0x00008000, /* 0.5000 */ \
    .run_decel          = 0x0000a000, /* 0.6250 */ \
    .air_accel          = 0x00006000, /* 0.3750 */ \
    .air_decel          = 0x00004000, /* 0.2500 */ \
    .gravity            = 0x00004000, /* 0.2500 */ \
    .apex_threshold     = 0x0000c000, /* 0.7500 */ \
    .fall_max           = 0x00040000, /* 4.0000 */ \
    .jump_speed         = 0x00042000, /* 4.1250 */ \
    .jump_cut           = 0x00018000, /* 1.5000 */ \
    .double_jump_speed  = 0x0003c800, /* 3.7812 */ \
    .dash_speed         = 0x000b8000, /* 11.5000 */ \
    .dash_decel         = 0x00010000, /* 1.0000 */ \
    .dash_frames        = 8, \
    .dash_cooldown      = 20, \
    .wall_slide_max     = 0x00010000, /* 1.0000 */ \
    .wall_jump_x        = 0x00040000, /* 4.0000 */ \
    .wall_jump_y        = 0x00044000, /* 4.2500 */ \
    .wall_jump_lock     = 8, \
    .wall_reach         = 2, \
    .climb_speed        = 0x00010000, /* 1.0000 */ \
    .pad_speed          = 0x00060000, /* 6.0000 */ \
    .coyote_frames      = 10, \
    .jump_buffer_frames = 5, \
    .drop_frames        = 8, \
    .hurt_frames        = 36, \
    .invuln_frames      = 60, \
    .attack_frames      = 14, \
    .attack_active      = 6, \
    .attack_cooldown    = 20, \
    .pogo_speed         = 0x00038000, /* 3.5000 */ \
    .knock_x            = 0x00030000, /* 3.0000 */ \
    .knock_y            = 0x00020000, /* 2.0000 */ \
    .knock_frames       = 12, \
    .shield_recharge    = 600, \
    .carry_speed        = 0x0001b99a, /* 1.7250 */ \
    .weight_speed       = 0x0001e666, /* 1.9000 */ \
    .weight_jump        = 0x00030000, /* 3.0000 */ \
    .fruit_min          = 900, \
    .fruit_range        = 600, \
    .ice_accel          = 0x00001000, /* 0.0625 */ \
    .ice_decel          = 0x00000ccd, /* 0.0500 */ \
    .belt_speed         = 0x0000c000, /* 0.7500 */ \
}

#endif
