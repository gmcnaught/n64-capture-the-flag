#include "audio.h"

const char *const audio_sfx_names[SFX_COUNT] = {
    "jump", "djump", "dash", "land", "attack", "hit", "kill",
    "hurt", "pogo", "pad", "pickup", "save", "shield", "menu", "room",
    "wind",
};

const char *const audio_music_names[MUSIC_COUNT + 1] = {0, "which_brand_of_mustard",
                                                              "napping_on_a_cloud"};

uint8_t audio_music(const Game *g)
{
    return g->title ? MUSIC_TITLE : g->lv.music;
}
