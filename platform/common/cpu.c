#include "cpu.h"

static BotNav nav;
static Bot bots[MAX_PLAYERS];
static bool have_level, on_title;

bool cpu_set_level(const Level *lv, const PlayerTuning *t, const uint8_t *data, uint32_t size)
{
    have_level = true;
    if (data && size && bot_nav_read(&nav, lv, t, data, size) == 0)
        return true;
    bot_nav_begin(&nav, lv, t);
    return false;
}

bool cpu_build(int n)
{
    return have_level && bot_nav_step(&nav, n);
}

bool cpu_ready(void)
{
    return have_level && bot_nav_ready(&nav);
}

void cpu_seed(uint32_t seed)
{
    for (int t = 0; t < MAX_PLAYERS; t++)
        bot_init(&bots[t], t, seed * 0x9e3779b9u + (uint32_t)t * 0x85ebca6bu + 1);
}

void cpu_play(const Game *g, uint8_t mask, InputState in[MAX_PLAYERS])
{
    if (g->title) {
        on_title = true;
        return;
    }
    if (on_title) {
        on_title = false;
        cpu_seed(g->frame);
    }
    for (int t = 0; t < g->player_count; t++)
        if (mask & (1u << t))
            in[t] = have_level ? bot_input(&bots[t], &nav, g) : (InputState){BTN_CONNECTED, 0, 0};
}

uint8_t cpu_mask(const Game *g)
{
    uint8_t m = 0;
    for (int t = 0; t < g->player_count; t++)
        if (game_is_cpu(g, t))
            m |= (uint8_t)(1u << t);
    return m;
}
