#include "cpu.h"

static BotNav nav;
static Bot bots[MAX_PLAYERS];
static uint8_t have_level, on_title;

int cpu_set_level(const Level *lv, const PlayerTuning *t, const uint8_t *data, uint32_t size)
{
    have_level = 1;
    if (data && size && bot_nav_read(&nav, lv, t, data, size) == 0)
        return 1;
    bot_nav_begin(&nav, lv, t);
    return 0;
}

int cpu_build(int n)
{
    return have_level && bot_nav_step(&nav, n);
}

int cpu_ready(void)
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
        on_title = 1;
        return;
    }
    if (on_title) {
        on_title = 0;
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
