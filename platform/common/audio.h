/* Platform audio: plays the core's sound events (Game.sfx) and the room's
 * music (Level.music). Sound files are build/assets/audio/<name>.wav
 * (host) or rom:/<name>.wav64 (N64), named by audio_sfx_names /
 * audio_music_names. Music is tracker modules (new-assets/music/<name>.xm):
 * rom:/<name>.xm64 on the N64, build/assets/audio/<name>.xm on the host
 * (libxm). */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#include "game.h"
#include "player.h"

#define MUSIC_COUNT 2           /* music ids 1..MUSIC_COUNT; 0 = silence */
#define MUSIC_TITLE 2           /* title screen track */

extern const char *const audio_sfx_names[SFX_COUNT];
extern const char *const audio_music_names[MUSIC_COUNT + 1];

/* Music id for the game's current screen: MUSIC_TITLE on the title
 * screen, else the level's. */
uint8_t audio_music(const Game *g);

int sound_open(const char *assets_dir);
/* Once per tick: start the sounds in sfx, switch music if it changed. */
void sound_update(uint32_t sfx, uint8_t music);
void sound_close(void);

#endif
