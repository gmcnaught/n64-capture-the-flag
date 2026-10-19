/* N64 (libdragon) build: plays the core in a 640x240 progressive
 * framebuffer; the shared scene renderer (platform/common/scene.c) draws
 * through rdpq. Sheets are built at sim resolution, so blits are 1:1 and
 * unflipped ones use copy mode. The mountains, trees and tile layers (up
 * to 1200 tiles) are drawn once per level into a 640x240 buffer with
 * transparent gaps; each frame draws the sky and drifting clouds, then
 * copies the buffer over them (copy mode skips its transparent texels).
 * Only the current backdrop's sky and scenery sheets and the level's
 * tileset sheet are in memory: a backdrop or stage change on the title
 * screen reloads them and rebuilds the buffer.
 *
 * Controllers 1 and 2: stick/D-pad move, A jump, Z or R dash, B attack,
 * Start start / pause (menu: up/down, A picks). Boots to the title screen
 * (up/down row, left/right stage or background; a stage change loads that
 * level).
 * Debug (controller 1): L next level, C-down restart (skips the title),
 * C-up debug overlay.
 *
 * Textures are RGBA16 (RGBA5551).
 *
 * Replay test: built with -DAUTOTEST (make -C platform/n64 test), or with
 * Start held at boot, plays every replay in rom:/replays.txt with rendering
 * and sound on and compares the final game_hash/kos with the host-recorded values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "audio.h"
#include "game.h"
#include "replay.h"
#include "scene.h"
#include "selftest.h"

#define MAX_LEVELS 16

/* Read by tools/ares_read.py over ares' GDB server for headless checks.
 * g_selftest_done is set last so a reader can tell the hash is valid. */
volatile uint32_t g_selftest_hash;
volatile uint32_t g_selftest_done;
volatile uint32_t g_frame;          /* game frame, for ares checks */
volatile uint32_t g_game_hash;      /* game_hash() after the last tick */
volatile uint32_t g_last_fb;        /* framebuffer of the last frame (ares_read.py --dump) */
volatile uint32_t g_frame_us;       /* last loop time, us (16683 = 60 Hz) */
volatile uint32_t g_frame_us_max;   /* worst loop time after the first 60 frames */
volatile uint32_t g_busy_us_max;    /* replay test: worst CPU + RDP time per frame */
volatile uint32_t g_repeats;        /* vblanks that re-showed the same framebuffer while measuring */
static volatile bool measuring;     /* count repeated frames (set during replays) */
volatile uint32_t g_replays_run, g_replays_passed;
volatile uint32_t g_autotest_done;  /* 0x600DF00D when the replay test finished */
volatile uint32_t g_music_chs;      /* music mixer channels playing (bit per channel from SFX_VOICES) */

static sprite_t *sprites[SPR_COUNT];
static char level_names[MAX_LEVELS][64];
static int level_count, level_index = -1;
static uint8_t *level_data;
static Level level;
static Game game;

static int read_list(const char *path, char names[][64], int max)
{
    int size, count = 0;
    char *txt = asset_load(path, &size);
    for (char *p = txt, *end = txt + size; p < end && count < max;) {
        char *nl = memchr(p, '\n', (size_t)(end - p));
        int n = (int)((nl ? nl : end) - p);
        if (n > 0 && n < 64) {
            memcpy(names[count], p, (size_t)n);
            names[count++][n] = 0;
        }
        p += n + 1;
    }
    free(txt);
    return count;
}

static int find_level(const char *name)
{
    for (int i = 0; i < level_count; i++)
        if (!strcmp(level_names[i], name))
            return i;
    return -1;
}

/* Load a level; the game must be re-initialized afterwards. */
static void load_level(int i)
{
    if (i == level_index)
        return;
    char path[80];
    int size;
    snprintf(path, sizeof path, "rom:/%s", level_names[i]);
    uint8_t *data = asset_load(path, &size);
    free(level_data);
    level_data = data;
    int err = level_load(&level, data, (uint32_t)size);
    assertf(err == 0, "%s: bad level data (%d)", path, err);
    level.name = level_names[i];
    level_index = i;
}

/* Title screen stage list: the level list, at the loaded level. */
static void set_stages(void)
{
    game.stage = (uint8_t)level_index;
    game.stage_count = (uint8_t)level_count;
}

static void read_input(joypad_port_t port, InputState *in)
{
    *in = (InputState){0, 0, 0};
    if (!joypad_is_connected(port))
        return;
    joypad_inputs_t j = joypad_get_inputs(port);
    in->buttons |= BTN_CONNECTED;
    if (j.btn.d_left) in->buttons |= BTN_LEFT;
    if (j.btn.d_right) in->buttons |= BTN_RIGHT;
    if (j.btn.d_up) in->buttons |= BTN_UP;
    if (j.btn.d_down) in->buttons |= BTN_DOWN;
    if (j.btn.a) in->buttons |= BTN_JUMP;
    if (j.btn.z || j.btn.r) in->buttons |= BTN_DASH;
    if (j.btn.b) in->buttons |= BTN_ATTACK;
    if (j.btn.start) in->buttons |= BTN_START;
    /* Stick: deadzone 8, full speed at 72 (typical N64 range is about +-80). */
    int sx = j.stick_x, sy = -j.stick_y;     /* N64 stick y is up-positive */
    if (sx > -8 && sx < 8) sx = 0;
    sx = sx * STICK_FULL / 72;
    sx = sx > STICK_FULL ? STICK_FULL : sx < -STICK_FULL ? -STICK_FULL : sx;
    if (sx && !(in->buttons & (BTN_LEFT | BTN_RIGHT))) {
        in->stick_x = (int8_t)sx;
        in->buttons |= sx < 0 ? BTN_LEFT : BTN_RIGHT;
    }
    if (sy > 40) in->buttons |= BTN_DOWN;
    if (sy < -40) in->buttons |= BTN_UP;
}

/* ---- DrawApi over rdpq -------------------------------------------------- */

/* RMODE_COPY (4 px/clock): unflipped blits with 1-bit alpha.
 * RMODE_FILL (4 px/clock): opaque rectangles. The rest is 1-cycle;
 * RMODE_TINT multiplies texels by the primitive color. */
enum { RMODE_NONE, RMODE_TEX, RMODE_FLAT, RMODE_COPY, RMODE_FILL, RMODE_TINT };
static int rmode;

static void use_mode(int m)
{
    if (rmode == m)
        return;
    rmode = m;
    if (m == RMODE_COPY) {
        rdpq_set_mode_copy(true);
        return;
    }
    if (m == RMODE_FILL) {
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
        return;
    }
    rdpq_set_mode_standard();
    if (m == RMODE_TEX) {
        rdpq_mode_alphacompare(1);
    } else if (m == RMODE_TINT) {
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (0, 0, 0, TEX0)));
        rdpq_mode_alphacompare(1);
    } else {
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    }
}

static void d_blit(void *ctx, int spr, int sx, int sy, int w, int h, int x, int y, int flip)
{
    (void)ctx;
    if (x >= VIEW_W || y >= VIEW_H || x + w <= 0 || y + h <= 0)
        return;
    use_mode(flip ? RMODE_TEX : RMODE_COPY);    /* copy mode can't flip */
    rdpq_sprite_blit(sprites[spr], (float)x, (float)y, &(rdpq_blitparms_t){
        .s0 = sx, .t0 = sy, .width = w, .height = h,
        .flip_x = (flip & FLIP_X) != 0, .flip_y = (flip & FLIP_Y) != 0,
    });
}

static void d_blit_tint(void *ctx, int spr, int sx, int sy, int w, int h, int x, int y,
                        int flip, uint32_t rgb)
{
    (void)ctx;
    if (x >= VIEW_W || y >= VIEW_H || x + w <= 0 || y + h <= 0)
        return;
    use_mode(RMODE_TINT);
    rdpq_set_prim_color(RGBA32(rgb >> 16, (rgb >> 8) & 0xff, rgb & 0xff, 255));
    rdpq_sprite_blit(sprites[spr], (float)x, (float)y, &(rdpq_blitparms_t){
        .s0 = sx, .t0 = sy, .width = w, .height = h,
        .flip_x = (flip & FLIP_X) != 0, .flip_y = (flip & FLIP_Y) != 0,
    });
}

static void d_fill(void *ctx, int x, int y, int w, int h, uint32_t c)
{
    (void)ctx;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > VIEW_W ? VIEW_W : x + w, y1 = y + h > VIEW_H ? VIEW_H : y + h;
    if (x0 >= x1 || y0 >= y1)
        return;
    color_t col = RGBA32(c >> 24, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
    if ((c & 0xff) == 0xff) {
        use_mode(RMODE_FILL);
        rdpq_set_fill_color(col);
    } else {
        use_mode(RMODE_FLAT);
        rdpq_set_prim_color(col);
    }
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

static void d_text(void *ctx, int x, int y, const char *s)
{
    (void)ctx;
    /* 8 px glyphs would look half as wide at 640: space them TEXT_CW apart
     * and embolden by drawing twice, 1 px apart. */
    const rdpq_textparms_t tp = {.char_spacing = TEXT_CW - 8};
    rdpq_text_printf(&tp, 1, x, y + 7, "%s", s);
    rdpq_text_printf(&tp, 1, x + 1, y + 7, "%s", s);
    rmode = RMODE_NONE;         /* rdpq_text changes the render mode */
}

static const DrawApi draw_api = {d_blit, d_blit_tint, d_fill, d_text, NULL};

/* ---- frame ---------------------------------------------------------------- */

static surface_t background;
static int background_level = -1;   /* level_index drawn into background */
static int sprites_backdrop = -1;   /* backdrop of sprites[SPR_SKY], sprites[SPR_SCENERY] */
static int sprites_tileset = -1;    /* tileset of sprites[SPR_TILES] */

static void load_sheet(int spr)
{
    char name[48], path[64];
    scene_sheet_name(spr, game.backdrop, scene_tileset(&game.lv), name, sizeof name);
    snprintf(path, sizeof path, "rom:/%s.sprite", name);
    if (sprites[spr])
        sprite_free(sprites[spr]);
    sprites[spr] = sprite_load(path);
}

/* Swap in the sheets of the game's backdrop and tileset if they changed
 * (after the RDP is done with the old ones). */
static void load_level_sheets(void)
{
    int tileset = scene_tileset(&game.lv);
    if (sprites_backdrop == game.backdrop && sprites_tileset == tileset)
        return;
    rspq_wait();
    if (sprites_backdrop != game.backdrop) {
        load_sheet(SPR_SKY);
        load_sheet(SPR_SCENERY);
    }
    if (sprites_tileset != tileset)
        load_sheet(SPR_TILES);
    sprites_backdrop = game.backdrop;
    sprites_tileset = tileset;
    background_level = -1;
}

static void build_background(void)
{
    if (!background.buffer)
        background = surface_alloc(FMT_RGBA16, VIEW_W, VIEW_H);
    rdpq_attach(&background, NULL);
    rdpq_clear(RGBA32(0, 0, 0, 0));     /* alpha 0: the sky shows through */
    rmode = RMODE_NONE;
    scene_draw_background(&game, &draw_api);
    rdpq_detach_wait();
    background_level = level_index;
}

static uint32_t selftest_hash;
static bool selftest_pass, debug;
static uint32_t last_ticks;

static void text(int x, int y, const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    d_text(NULL, x, y, buf);
}

/* Draw the game; returns after the frame is queued (or, with wait, after
 * the RDP finished it). */
static void render(const char *status, bool wait)
{
    load_level_sheets();
    if (background_level != level_index)
        build_background();
    surface_t *disp = display_get();
    uint32_t start = TICKS_READ();
    g_last_fb = (uint32_t)disp->buffer;
    rdpq_attach(disp, NULL);
    rmode = RMODE_NONE;
    scene_draw_sky(&game, &draw_api);
    use_mode(RMODE_COPY);
    rdpq_tex_blit(&background, 0, 0, NULL);
    scene_draw(&game, &draw_api, debug);

    if (!selftest_pass || debug)
        text(VIEW_W - TEXT_CW * 17, VIEW_H - 12, "ST %08lx %s", (unsigned long)selftest_hash,
             selftest_pass ? "PASS" : "FAIL");
    if (status)
        text(8, VIEW_H - 12, "%s", status);
    if (debug) {
        for (int t = 0; t < TEAM_COUNT; t++) {
            const Body *b = &game.players[t].body;
            text(8, 32 + t * 10, "P%d x %d y %d", t + 1, b->x, b->y);
        }
        text(8, 52, "frame %lu us  f %lu  %08lx", (unsigned long)g_frame_us,
             (unsigned long)game.frame, (unsigned long)g_game_hash);
    }
    if (wait) {
        rdpq_detach_wait();
        uint32_t busy = TICKS_TO_US(TICKS_READ() - start);
        if (busy > g_busy_us_max)
            g_busy_us_max = busy;
        display_show(disp);
    } else {
        rdpq_detach_show();
    }

    uint32_t now = TICKS_READ();
    g_frame_us = TICKS_TO_US(now - last_ticks);
    last_ticks = now;
    if (game.frame > 60 && g_frame_us > g_frame_us_max)
        g_frame_us_max = g_frame_us;
}

static void tick(const InputState in[TEAM_COUNT])
{
    game_tick(&game, in);
    g_frame = game.frame;
    g_game_hash = game_hash(&game);
}

/* Each vblank: a framebuffer origin equal to the previous vblank's means the
 * same frame was shown twice (a dropped frame at 60 Hz). */
static void count_repeats(void *arg)
{
    static uint32_t last_origin;
    uint32_t origin = vi_read(VI_ORIGIN);
    if (measuring && origin == last_origin)
        g_repeats++;
    last_origin = origin;
}

/* ---- replay test ------------------------------------------------------------ */

#define MAX_REPLAYS 24

static void autotest(void)
{
    static char names[MAX_REPLAYS][64], results[MAX_REPLAYS][64];
    int count = read_list("rom:/replays.txt", names, MAX_REPLAYS);

    for (int i = 0; i < count; i++) {
        char path[80], status[80];
        Replay r;
        snprintf(path, sizeof path, "rom:/%.60s", names[i]);
        int li = -1;
        if (replay_load(&r, path) == 0)
            li = find_level(r.level);
        if (li < 0) {
            snprintf(results[i], 64, "%-22.22s LOAD FAIL", names[i]);
            g_replays_run++;
            continue;
        }
        load_level(li);
        replay_start(&r, &game, &level);
        for (size_t f = 0; f < r.count; f++) {
            tick(r.frames[f].in);
            sound_update(game.sfx, audio_music(&game));
            snprintf(status, sizeof status, "replay %d/%d %.40s", i + 1, count, names[i]);
            render(status, true);
            if (f == 3)             /* queue primed after the load */
                measuring = true;
        }
        measuring = false;
        bool ok = (!r.has_hash || r.hash == g_game_hash) &&
                  (!r.has_kos || (r.kos[0] == game.kos[0] && r.kos[1] == game.kos[1]));
        snprintf(results[i], 64, "%-22.22s %08lx %s", names[i], (unsigned long)g_game_hash,
                 ok ? "PASS" : "FAIL");
        debugf("replay %s: hash %08lx kos %d,%d %s\n", names[i], (unsigned long)g_game_hash,
               game.kos[0], game.kos[1], ok ? "PASS" : "FAIL");
        g_replays_run++;
        g_replays_passed += ok;
        replay_free(&r);
    }
    g_autotest_done = 0x600DF00D;

    while (1) {
        sound_update(0, 0);         /* music off on the results screen */
        surface_t *disp = display_get();
        rdpq_attach_clear(disp, NULL);
        rmode = RMODE_NONE;
        text(16, 4, "REPLAY TEST %lu/%lu PASS", (unsigned long)g_replays_passed,
             (unsigned long)g_replays_run);
        for (int i = 0; i < count; i++)
            text(16, 16 + i * 10, "%s", results[i]);
        text(16, 26 + count * 10, "worst frame %lu us", (unsigned long)g_frame_us_max);
        text(16, 36 + count * 10, "worst CPU+RDP %lu us", (unsigned long)g_busy_us_max);
        text(16, 46 + count * 10, "repeated frames %lu", (unsigned long)g_repeats);
        rdpq_detach_show();
    }
}

/* ---- audio ------------------------------------------------------------------ */

/* Sound effects share SFX_VOICES mixer channels, round robin. Music is
 * tracker modules (rom:/<name>.xm64) in a blue/green pair of players: the
 * active one is heard; a switch to a song that is not loaded loads it into
 * the inactive player, a switch to a loaded one seeks it to the top, and
 * the volumes swap. The silenced player keeps running: stopping or closing a playing module
 * (xm64player_stop/close) hung the RSP queue in ares with libdragon
 * preview. So a player is loaded only while empty,
 * which with two songs is enough; reloading one needs that fixed. Each
 * player gets its mixer channels when first loaded. */
#define SFX_VOICES 8
#define MUSIC_VOL 0.45f
static wav64_t sfx_wav[SFX_COUNT];
static struct {
    xm64player_t xm;
    uint8_t song;               /* music id loaded, 0 = empty */
} music[2];
static uint8_t cur_music, active, next_voice, next_ch = SFX_VOICES;

int sound_open(const char *dir)
{
    char path[64];
    (void)dir;
    audio_init(22050, 4);
    mixer_init(MIXER_MAX_CHANNELS);
    for (int i = 0; i < SFX_COUNT; i++) {
        snprintf(path, sizeof path, "rom:/%s.wav64", audio_sfx_names[i]);
        wav64_open(&sfx_wav[i], path);
    }
    for (int i = 0; i < SFX_VOICES; i++)
        mixer_ch_set_vol(i, 0.6f, 0.6f);
    return 0;
}

/* Make song m (0 = silence) the heard one, from its beginning. */
static void music_switch(uint8_t m)
{
    int p = music[0].song == m ? 0 : music[1].song == m ? 1 : -1;
    if (m && p >= 0) {
        xm64player_seek(&music[p].xm, 0, 0, 0);     /* already loaded: back to the top */
    } else if (m) {
        p = !active;
        if (music[p].song == 0 && music[!p].song == 0)
            p = active;
        assertf(music[p].song == 0, "music %d: both players busy (no reload)", m);
        char path[64];
        snprintf(path, sizeof path, "rom:/%s.xm64", audio_music_names[m]);
        xm64player_open(&music[p].xm, path);
        int n = xm64player_num_channels(&music[p].xm);
        assertf(next_ch + n <= MIXER_MAX_CHANNELS, "%s: out of mixer channels", path);
        xm64player_set_loop(&music[p].xm, true);
        xm64player_set_vol(&music[p].xm, 0);
        xm64player_play(&music[p].xm, next_ch);
        next_ch = (uint8_t)(next_ch + n);
        music[p].song = m;
    }
    for (int i = 0; i < 2; i++)
        if (music[i].song)
            xm64player_set_vol(&music[i].xm, i == p ? MUSIC_VOL : 0);
    if (p >= 0)
        active = (uint8_t)p;
}

void sound_update(uint32_t bits, uint8_t m)
{
    for (int i = 0; i < SFX_COUNT; i++)
        if (bits & SFX_BIT(i)) {
            wav64_play(&sfx_wav[i], next_voice);
            next_voice = (uint8_t)((next_voice + 1) % SFX_VOICES);
        }
    if (m > MUSIC_COUNT)
        m = 0;
    if (m != cur_music) {
        music_switch(m);
        cur_music = m;
    }
    uint32_t chs = 0;
    for (int i = SFX_VOICES; i < MIXER_MAX_CHANNELS; i++)
        chs |= (uint32_t)mixer_ch_playing(i) << (i - SFX_VOICES);
    g_music_chs = chs;
    mixer_try_play();
}

void sound_close(void) {}

int main(void)
{
    debug_init_emulog();
    debug_init_usblog();

    selftest_hash = core_selftest();
    g_selftest_hash = selftest_hash;
    g_selftest_done = 0x600DF00D;
    selftest_pass = selftest_hash == CORE_SELFTEST_EXPECTED;
    debugf("core_selftest=%08lx %s\n", (unsigned long)selftest_hash,
           selftest_pass ? "PASS" : "FAIL");

    dfs_init(DFS_DEFAULT_LOCATION);
    joypad_init();
    /* 640x240 progressive; the VI resample filter can artifact above 400
     * wide, so filters are off. */
    display_init(RESOLUTION_640x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_DISABLED);
    rdpq_init();
    vi_install_vblank_handler(count_repeats, NULL);
    rdpq_text_register_font(1, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    for (int i = 0; i < SPR_COUNT; i++) {
        char name[48], path[64];
        if (i == SPR_SKY || i == SPR_SCENERY || i == SPR_TILES)
            continue;               /* per backdrop and tileset: load_level_sheets */
        scene_sheet_name(i, 0, 0, name, sizeof name);
        snprintf(path, sizeof path, "rom:/%s.sprite", name);
        sprites[i] = sprite_load(path);
    }
    level_count = read_list("rom:/levels.txt", level_names, MAX_LEVELS);
    assertf(level_count > 0, "no levels in rom:/levels.txt");
    last_ticks = TICKS_READ();

    sound_open(NULL);
    joypad_poll();
#ifdef AUTOTEST
    autotest();
#endif
    if (joypad_get_buttons_held(JOYPAD_PORT_1).start)
        autotest();

    load_level(0);
    game_init(&game, &level, NULL, ABIL_ALL);
    set_stages();
    game_title(&game);
    while (1) {
        joypad_poll();
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.l) {            /* debug: next level */
            load_level((level_index + 1) % level_count);
            game_init(&game, &level, NULL, ABIL_ALL);
            set_stages();
        } else if (pressed.c_down) {
            game_init(&game, &level, NULL, ABIL_ALL);
            set_stages();
        }
        if (pressed.c_up)
            debug = !debug;

        InputState in[TEAM_COUNT];
        read_input(JOYPAD_PORT_1, &in[0]);
        read_input(JOYPAD_PORT_2, &in[1]);
        tick(in);
        if (game.stage != level_index) {
            load_level(game.stage);
            game_set_level(&game, &level);
        }
        sound_update(game.sfx, audio_music(&game));
        render(NULL, false);
    }
}
