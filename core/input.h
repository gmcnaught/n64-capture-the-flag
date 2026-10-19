/* Per-frame input, filled by the platform layer. */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

enum {
    BTN_LEFT   = 1 << 0,
    BTN_RIGHT  = 1 << 1,
    BTN_UP     = 1 << 2,
    BTN_DOWN   = 1 << 3,
    BTN_JUMP   = 1 << 4,
    BTN_DASH   = 1 << 5,
    BTN_ATTACK = 1 << 6,
    BTN_START  = 1 << 7,
    BTN_CONNECTED = 1 << 15,    /* controller plugged in (used for player 2) */
};

/* Analog stick, screen orientation (+x right, +y down). Full deflection is
 * STICK_FULL; platforms scale their hardware range to it and also set the
 * direction buttons from the stick. 0 = digital input only. */
#define STICK_FULL 64

typedef struct {
    uint16_t buttons;
    int8_t stick_x, stick_y;
} InputState;

#endif
