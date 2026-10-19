/* Host audio (SDL3): one stream per sound effect (a new event restarts it)
 * and one music stream, all bound to the default device, which mixes them.
 * Music is tracker modules (build/assets/audio/<name>.xm) played with libxm
 * (external/libxm): the current module, restarted from its beginning on a
 * switch, renders into the music stream, topped up every tick. */
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "audio.h"
#include "xm.h"

#define MUSIC_RATE 48000
#define MUSIC_AHEAD 4096        /* frames kept queued (85 ms) */
#define MUSIC_CHUNK 1024        /* frames rendered at a time */

typedef struct {
    Uint8 *buf;
    Uint32 len;
    SDL_AudioStream *stream;
} Sound;

static SDL_AudioDeviceID dev;
static Sound sfx[SFX_COUNT];
static xm_context_t *music[MUSIC_COUNT + 1];
static SDL_AudioStream *music_stream;
static uint8_t cur_music;

static void *read_all(const char *path, Uint32 *len)
{
    size_t n;
    void *data = SDL_LoadFile(path, &n);
    *len = (Uint32)n;
    return data;
}

static xm_context_t *load_module(const char *dir, const char *name)
{
    char path[512];
    Uint32 len;
    snprintf(path, sizeof path, "%s/audio/%s.xm", dir, name);
    char *data = read_all(path, &len);
    if (!data) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return NULL;
    }
    xm_prescan_data_t *p = SDL_malloc(XM_PRESCAN_DATA_SIZE);
    xm_context_t *ctx = NULL;
    if (xm_prescan_module(data, len, p)) {
        ctx = xm_create_context(malloc(xm_size_for_context(p)), p, data, len);
        xm_set_sample_rate(ctx, MUSIC_RATE);
    } else {
        fprintf(stderr, "audio: %s: not a module libxm can play\n", path);
    }
    SDL_free(p);
    SDL_free(data);
    return ctx;
}

static void load(Sound *s, const char *dir, const char *name, float gain)
{
    char path[512];
    SDL_AudioSpec spec;
    snprintf(path, sizeof path, "%s/audio/%s.wav", dir, name);
    if (!SDL_LoadWAV(path, &spec, &s->buf, &s->len)) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return;
    }
    s->stream = SDL_CreateAudioStream(&spec, NULL);
    SDL_SetAudioStreamGain(s->stream, gain);
    SDL_BindAudioStream(dev, s->stream);
}

int sound_open(const char *assets_dir)
{
    dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!dev) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return 1;
    }
    for (int i = 0; i < SFX_COUNT; i++)
        load(&sfx[i], assets_dir, audio_sfx_names[i], 0.6f);
    for (int i = 1; i <= MUSIC_COUNT; i++)
        music[i] = load_module(assets_dir, audio_music_names[i]);
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, MUSIC_RATE};
    music_stream = SDL_CreateAudioStream(&spec, NULL);
    SDL_SetAudioStreamGain(music_stream, 0.45f);
    SDL_BindAudioStream(dev, music_stream);
    return 0;
}

void sound_update(uint32_t bits, uint8_t m)
{
    if (!dev)
        return;
    for (int i = 0; i < SFX_COUNT; i++) {
        if ((bits & SFX_BIT(i)) && sfx[i].stream) {
            SDL_ClearAudioStream(sfx[i].stream);
            SDL_PutAudioStreamData(sfx[i].stream, sfx[i].buf, (int)sfx[i].len);
        }
    }
    if (m > MUSIC_COUNT)
        m = 0;
    if (m != cur_music) {
        SDL_ClearAudioStream(music_stream);
        if (music[m]) {
            xm_reset_context(music[m]);     /* from the top */
            xm_set_sample_rate(music[m], MUSIC_RATE);
        }
        cur_music = m;
    }
    xm_context_t *ctx = music[cur_music];
    if (!ctx)
        return;
    static float frames[MUSIC_CHUNK * 2];
    while (SDL_GetAudioStreamQueued(music_stream) < MUSIC_AHEAD * (int)sizeof frames[0] * 2) {
        xm_generate_samples(ctx, frames, MUSIC_CHUNK);
        SDL_PutAudioStreamData(music_stream, frames, (int)sizeof frames);
    }
}

void sound_close(void)
{
    for (int i = 0; i < SFX_COUNT; i++)
        SDL_DestroyAudioStream(sfx[i].stream);
    for (int i = 1; i <= MUSIC_COUNT; i++)
        free(music[i]);
    SDL_DestroyAudioStream(music_stream);
    if (dev)
        SDL_CloseAudioDevice(dev);
}
