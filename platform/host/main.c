/* Host (SDL3) build: plays the core with keyboard/gamepads, debug overlay,
 * live tuning and replays. The 640x240 view is shown as 640x480 (each
 * pixel doubled vertically), the 4:3 shape a TV gives the N64's 640x240.
 *
 * usage: ctf [options]
 *   --selftest          print the core self-test hash and exit (no window)
 *   --assets DIR        asset directory (default build/assets)
 *   --level FILE        level file in DIR/levels (default: first in levels.txt)
 *   --record FILE       record input to a replay file, written on exit
 *   --replay FILE       play a replay, then continue with live input
 *   --headless          with --replay: run without a window, print the result
 *   --check             with --headless: exit 1 if hash/kos differ from the file
 *   --update            with --headless: rewrite the file's hash/kos lines
 *   --trace             with --headless: print both players every frame
 *   --frames N          quit after N frames
 *   --screenshot FILE   save the last frame as BMP on exit
 *   --mute              no audio
 *   --fullscreen        start fullscreen (F11 or Alt+Enter toggles)
 *
 * Keys: P1 WASD move, Space jump, F attack, Left Shift dash; P2 arrows
 * move, Right Shift jump, / attack, . dash; Enter start / pause (menu:
 * up/down, jump picks). Gamepad 1 also controls P1, gamepad 2 P2. Starts
 * on the title screen (a replay starts in play).
 * Debug: Backspace restart (skips the title), Tab next level, P freeze, N step one frame
 * (frozen), F1 debug overlay, F2 tuning panel ([ ] select, - = adjust,
 * Shift = x16, F5 write core/player_tuning.h, F9 defaults), F11 /
 * Alt+Enter fullscreen, Esc quit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "audio.h"
#include "game.h"
#include "replay.h"
#include "scene.h"
#include "selftest.h"

#define SCALE 2                 /* window = 640x480 view times this */
#define YS 2                    /* view px per sim px, vertically */
#define MAX_LEVELS 16

#ifndef TUNING_HEADER_PATH
#define TUNING_HEADER_PATH "core/player_tuning.h"
#endif

static const char *assets_dir = "build/assets";

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n > 0 ? (size_t)n : 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return data;
}

/* ---- levels ------------------------------------------------------------ */

static char level_names[MAX_LEVELS][64];
static int level_count;

static void load_level_list(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/levels/levels.txt", assets_dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (level_count < MAX_LEVELS && fgets(level_names[level_count], 64, f)) {
        level_names[level_count][strcspn(level_names[level_count], "\r\n")] = 0;
        if (level_names[level_count][0])
            level_count++;
    }
    fclose(f);
}

static uint8_t *level_data;
static Level level;

/* The game keeps pointers into level_data: reload only before game_init. */
static int load_level(const char *name)
{
    char path[512];
    uint32_t size;
    snprintf(path, sizeof path, "%s/levels/%s", assets_dir, name);
    uint8_t *data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "cannot read %s (run `make assets`)\n", path);
        return 1;
    }
    int err = level_load(&level, data, size);
    if (err) {
        fprintf(stderr, "%s: bad level data (%d)\n", path, err);
        exit(1);
    }
    free(level_data);
    level_data = data;
    return 0;
}

/* ---- replays ----------------------------------------------------------- */

static int run_headless(const char *path, int check, int update, long max_frames, int trace)
{
    Replay r;
    Game g;
    if (replay_load(&r, path) || load_level(r.level))
        return 2;
    replay_start(&r, &g, &level);
    size_t n = r.count;
    if (max_frames >= 0 && (size_t)max_frames < n)
        n = (size_t)max_frames;
    for (size_t i = 0; i < n; i++) {
        game_tick(&g, r.frames[i].in);
        if (trace)
            for (int t = 0; t < TEAM_COUNT; t++) {
                const Player *p = &g.players[t];
                printf("%lu P%d x %d y %d vx %.3f vy %.3f st %d gnd %d hp %d atk %d dead %d "
                       "flag %d wins %d winner %d fruit %d spot %d water %d caught %d "
                       "title %d paused %d menu %d wfx %d wfy %d rx %ld cd %d\n",
                       (unsigned long)g.frame, t + 1, p->body.x, p->body.y, p->body.vx / 65536.0,
                       p->body.vy / 65536.0, p->state, p->grounded, p->health, p->attack_timer,
                       p->dead, g.carrying[t], g.wins[t], g.winner, g.fruit_active,
                       g.fruit_spot, g.water_len, g.caught[t], g.title, g.paused, g.menu_sel,
                       g.water_len ? g.water_cx[0] : -1, g.water_len ? g.water_cy[0] : -1,
                       (long)p->body.rem_x, g.countdown);
            }
    }
    uint32_t hash = game_hash(&g);
    printf("%s: frames=%lu hash=%08lx kos=%d,%d", path, (unsigned long)n, (unsigned long)hash,
           g.kos[0], g.kos[1]);
    int ok = 1;
    if (r.has_hash && r.hash != hash) {
        printf(" (expected hash %08lx)", (unsigned long)r.hash);
        ok = 0;
    }
    if (r.has_kos && (r.kos[0] != g.kos[0] || r.kos[1] != g.kos[1])) {
        printf(" (expected kos %d,%d)", r.kos[0], r.kos[1]);
        ok = 0;
    }
    printf(" %s\n", (r.has_hash || r.has_kos) ? (ok ? "PASS" : "FAIL") : "");
    if (update) {
        r.has_hash = r.has_kos = 1;
        r.hash = hash;
        r.kos[0] = g.kos[0];
        r.kos[1] = g.kos[1];
        if (replay_save(&r, path))
            return 2;
        printf("updated %s\n", path);
    }
    replay_free(&r);
    return check && !ok ? 1 : 0;
}

/* ---- drawing ----------------------------------------------------------- */

static SDL_Renderer *ren;
static SDL_Texture *sprites[SPR_COUNT];

static SDL_Texture *load_rgba(const char *name)
{
    char path[512];
    uint32_t size;
    snprintf(path, sizeof path, "%s/host/%s.rgba", assets_dir, name);
    uint8_t *d = read_file(path, &size);
    if (!d || size < 8 || memcmp(d, "RGBA", 4)) {
        fprintf(stderr, "cannot read %s (run `make assets`)\n", path);
        free(d);
        return NULL;
    }
    int w = d[4] << 8 | d[5], h = d[6] << 8 | d[7];
    if (size != 8u + (uint32_t)w * h * 4) {
        fprintf(stderr, "%s: bad size\n", path);
        free(d);
        return NULL;
    }
    /* Reduce to RGBA5551 like the N64's RGBA16 textures. */
    for (uint8_t *px = d + 8; px < d + size; px += 4) {
        for (int c = 0; c < 3; c++)
            px[c] = (uint8_t)((px[c] >> 3) * 255 / 31);
        px[3] = px[3] >= 128 ? 255 : 0;
    }
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, NULL, d + 8, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    free(d);
    return t;
}

static void d_blit(void *ctx, int spr, int sx, int sy, int w, int h, int x, int y, int flip)
{
    (void)ctx;
    SDL_FRect src = {(float)sx, (float)sy, (float)w, (float)h};
    SDL_FRect dst = {(float)x, (float)(y * YS), (float)w, (float)(h * YS)};
    SDL_FlipMode f = (SDL_FlipMode)(((flip & FLIP_X) ? SDL_FLIP_HORIZONTAL : 0) |
                                    ((flip & FLIP_Y) ? SDL_FLIP_VERTICAL : 0));
    SDL_RenderTextureRotated(ren, sprites[spr], &src, &dst, 0, NULL, f);
}

static void d_blit_tint(void *ctx, int spr, int sx, int sy, int w, int h, int x, int y,
                        int flip, uint32_t rgb)
{
    SDL_SetTextureColorMod(sprites[spr], (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb);
    d_blit(ctx, spr, sx, sy, w, h, x, y, flip);
    SDL_SetTextureColorMod(sprites[spr], 255, 255, 255);
}

static void d_fill(void *ctx, int x, int y, int w, int h, uint32_t c)
{
    (void)ctx;
    SDL_FRect rc = {(float)x, (float)(y * YS), (float)w, (float)(h * YS)};
    SDL_SetRenderDrawColor(ren, (Uint8)(c >> 24), (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c);
    SDL_RenderFillRect(ren, &rc);
}

static void d_text(void *ctx, int x, int y, const char *s)
{
    (void)ctx;
    /* 8x8 glyphs scaled 2x: TEXT_CW x TEXT_CH sim px per character. */
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_SetRenderScale(ren, 2, YS);
    SDL_RenderDebugText(ren, (float)x / 2, (float)y, s);
    SDL_SetRenderScale(ren, 1, 1);
}

static const DrawApi draw_api = {d_blit, d_blit_tint, d_fill, d_text, NULL};

static void text_bg(int x, int y, const char *s)
{
    d_fill(NULL, x - 2, y - 1, (int)strlen(s) * TEXT_CW + 4, TEXT_CH + 2, 0x000000a0);
    d_text(NULL, x, y, s);
}

/* Q16.16 as a signed decimal with 2 places. */
static const char *fxs(char *buf, fx v)
{
    unsigned long a = (unsigned long)(v < 0 ? -(long)v : v);
    snprintf(buf, 16, "%c%lu.%02lu", v < 0 ? '-' : '+', a >> 16, ((a & 0xffff) * 100) >> 16);
    return buf;
}

static void render_debug_text(const Game *g)
{
    static const char *states[] = {"NORMAL", "DASH", "CLIMB", "HURT"};
    char l[80], f1[16], f2[16];
    int y = 24;
    for (int t = 0; t < TEAM_COUNT; t++) {
        const Player *p = &g->players[t];
        const Body *b = &p->body;
        snprintf(l, sizeof l, "P%d x %d y %d vx %s vy %s", t + 1, b->x, b->y, fxs(f1, b->vx),
                 fxs(f2, b->vy));
        text_bg(8, y += 10, l);
        snprintf(l, sizeof l, "  %s gnd %d wall %+d atk %d hp %d", states[p->state], p->grounded,
                 p->wall_dir, p->attack_timer, p->health);
        text_bg(8, y += 10, l);
    }
    snprintf(l, sizeof l, "f %lu  %08lx", (unsigned long)g->frame, (unsigned long)game_hash(g));
    text_bg(8, y += 10, l);
}

static void render_tuning(const PlayerTuning *t, int sel)
{
    char l[64], f[16];
    for (int i = 0; i < tuning_field_count; i++) {
        int32_t v = *(const int32_t *)((const char *)t + tuning_fields[i].offset);
        if (tuning_fields[i].is_fx)
            snprintf(l, sizeof l, "%c%-18s %s", i == sel ? '>' : ' ', tuning_fields[i].name, fxs(f, v));
        else
            snprintf(l, sizeof l, "%c%-18s %ld", i == sel ? '>' : ' ', tuning_fields[i].name, (long)v);
        text_bg(VIEW_W - TEXT_CW * 28 - 4, 4 + i * 6, l);
    }
}

static int write_tuning_header(const PlayerTuning *t)
{
    FILE *f = fopen(TUNING_HEADER_PATH, "w");
    if (!f) {
        perror(TUNING_HEADER_PATH);
        return 1;
    }
    fprintf(f, "/* Player movement tuning. Q16.16 values are px/frame (speeds) or\n"
               " * px/frame^2 (accelerations) at 60 Hz; the comment shows the decimal value.\n"
               " * The host build rewrites this file from its tuning panel (F5). */\n"
               "#ifndef PLAYER_TUNING_H\n#define PLAYER_TUNING_H\n\n"
               "#define PLAYER_TUNING_DEFAULTS { \\\n");
    for (int i = 0; i < tuning_field_count; i++) {
        int32_t v = *(const int32_t *)((const char *)t + tuning_fields[i].offset);
        char name[32];
        snprintf(name, sizeof name, ".%s", tuning_fields[i].name);
        if (tuning_fields[i].is_fx)
            fprintf(f, "    %-19s = 0x%08lx, /* %.4f */ \\\n", name, (unsigned long)(uint32_t)v,
                    v / 65536.0);
        else
            fprintf(f, "    %-19s = %ld, \\\n", name, (long)v);
    }
    fprintf(f, "}\n\n#endif\n");
    fclose(f);
    printf("wrote %s\n", TUNING_HEADER_PATH);
    return 0;
}

/* ---- input ------------------------------------------------------------- */

static SDL_Gamepad *pads[TEAM_COUNT];

static void read_pad(SDL_Gamepad *pad, InputState *in)
{
    in->buttons |= BTN_CONNECTED;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) in->buttons |= BTN_LEFT;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) in->buttons |= BTN_RIGHT;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) in->buttons |= BTN_UP;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) in->buttons |= BTN_DOWN;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) in->buttons |= BTN_JUMP;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) ||
        SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) in->buttons |= BTN_DASH;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST)) in->buttons |= BTN_ATTACK;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START)) in->buttons |= BTN_START;
    /* Stick: deadzone, scale to STICK_FULL at ~70% deflection (N64-like). */
    int sx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 360;
    int sy = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 360;
    if (sx > -8 && sx < 8) sx = 0;
    if (sy > -8 && sy < 8) sy = 0;
    sx = sx > STICK_FULL ? STICK_FULL : sx < -STICK_FULL ? -STICK_FULL : sx;
    sy = sy > STICK_FULL ? STICK_FULL : sy < -STICK_FULL ? -STICK_FULL : sy;
    if (sx && !(in->buttons & (BTN_LEFT | BTN_RIGHT))) {
        in->stick_x = (int8_t)sx;
        in->buttons |= sx < 0 ? BTN_LEFT : BTN_RIGHT;
    }
    if (sy > STICK_FULL / 2) in->buttons |= BTN_DOWN;
    if (sy < -STICK_FULL / 2) in->buttons |= BTN_UP;
}

/* Keyboard layout per player: left, right, up, down, jump, attack, dash. */
static const SDL_Scancode KEYS[TEAM_COUNT][7] = {
    {SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S,
     SDL_SCANCODE_SPACE, SDL_SCANCODE_F, SDL_SCANCODE_LSHIFT},
    {SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
     SDL_SCANCODE_RSHIFT, SDL_SCANCODE_SLASH, SDL_SCANCODE_PERIOD},
};
static const uint16_t KEY_BITS[7] = {
    BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_JUMP, BTN_ATTACK, BTN_DASH,
};

static void poll_input(InputState in[TEAM_COUNT])
{
    const bool *k = SDL_GetKeyboardState(NULL);
    for (int t = 0; t < TEAM_COUNT; t++) {
        in[t] = (InputState){BTN_CONNECTED, 0, 0};
        for (int i = 0; i < 7; i++)
            if (k[KEYS[t][i]])
                in[t].buttons |= KEY_BITS[i];
        if (pads[t])
            read_pad(pads[t], &in[t]);
    }
    if (k[SDL_SCANCODE_RETURN])
        in[0].buttons |= BTN_START;
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
    long max_frames = -1;
    const char *level_arg = NULL, *record_path = NULL, *replay_path = NULL, *shot_path = NULL;
    int headless = 0, check = 0, update = 0, trace = 0, mute = 0, fullscreen = 0;
    uint32_t hash = core_selftest();
    int pass = hash == CORE_SELFTEST_EXPECTED;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--selftest")) {
            printf("core_selftest=%08lx %s\n", (unsigned long)hash, pass ? "PASS" : "FAIL");
            return pass ? 0 : 1;
        } else if (!strcmp(a, "--frames") && i + 1 < argc) {
            max_frames = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--assets") && i + 1 < argc) {
            assets_dir = argv[++i];
        } else if (!strcmp(a, "--level") && i + 1 < argc) {
            level_arg = argv[++i];
        } else if (!strcmp(a, "--mute")) {
            mute = 1;
        } else if (!strcmp(a, "--fullscreen")) {
            fullscreen = 1;
        } else if (!strcmp(a, "--record") && i + 1 < argc) {
            record_path = argv[++i];
        } else if (!strcmp(a, "--replay") && i + 1 < argc) {
            replay_path = argv[++i];
        } else if (!strcmp(a, "--headless")) {
            headless = 1;
        } else if (!strcmp(a, "--check")) {
            check = 1;
        } else if (!strcmp(a, "--update")) {
            update = 1;
        } else if (!strcmp(a, "--trace")) {
            trace = 1;
        } else if (!strcmp(a, "--screenshot") && i + 1 < argc) {
            shot_path = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--selftest] [--assets DIR] [--level FILE] [--mute] "
                            "[--fullscreen] [--record FILE] "
                            "[--replay FILE [--headless [--check|--update] [--trace]]] "
                            "[--frames N] [--screenshot FILE]\n", argv[0]);
            return 2;
        }
    }
    printf("core_selftest=%08lx %s\n", (unsigned long)hash, pass ? "PASS" : "FAIL");
    if (headless) {
        if (!replay_path) {
            fprintf(stderr, "--headless needs --replay\n");
            return 2;
        }
        return run_headless(replay_path, check, update, max_frames, trace);
    }

    load_level_list();
    Replay playback = {0};
    size_t playback_pos = 0;
    int playing = 0;
    PlayerTuning tuning;
    tuning_defaults(&tuning);
    uint8_t abilities = ABIL_ALL;
    char cur_level[64];
    if (replay_path) {
        if (replay_load(&playback, replay_path))
            return 2;
        level_arg = playback.level;
        playing = 1;
    }
    if (!level_arg && level_count == 0) {
        fprintf(stderr, "no levels in %s/levels/levels.txt (run `make assets`)\n", assets_dir);
        return 1;
    }
    snprintf(cur_level, sizeof cur_level, "%s", level_arg ? level_arg : level_names[0]);
    if (load_level(cur_level))
        return 1;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (mute ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win;
    if (!SDL_CreateWindowAndRenderer("n64-capture-the-flag (host)", VIEW_W * SCALE,
                                     VIEW_H * YS * SCALE, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(ren, VIEW_W, VIEW_H * YS, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    if (fullscreen)
        SDL_SetWindowFullscreen(win, true);
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < SPR_COUNT; i++)
        if (!(sprites[i] = load_rgba(scene_sprite_names[i])))
            return 1;
    if (!mute)
        sound_open(assets_dir);

    Game g;
    if (playing) {
        replay_start(&playback, &g, &level);
    } else {
        game_init(&g, &level, &tuning, abilities);
        game_title(&g);
    }
    Replay rec;
    if (record_path)
        replay_init(&rec, cur_level, g.abilities, &g.tuning);

    int debug = 0, tuning_open = 0, sel = 0, frozen = 0, step = 0;
    Uint64 next = SDL_GetTicksNS();
    const Uint64 tick_ns = 1000000000ull / 60;
    long frames = 0;
    int running = 1;
    while (running && (max_frames < 0 || frames < max_frames)) {
        SDL_Event ev;
        int restart = 0;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = 0;
            } else if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
                for (int i = 0; i < TEAM_COUNT; i++)
                    if (!pads[i]) {
                        pads[i] = SDL_OpenGamepad(ev.gdevice.which);
                        break;
                    }
            } else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
                for (int i = 0; i < TEAM_COUNT; i++)
                    if (pads[i] && SDL_GetGamepadID(pads[i]) == ev.gdevice.which) {
                        SDL_CloseGamepad(pads[i]);
                        pads[i] = NULL;
                    }
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode key = ev.key.key;
                int big = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
                int32_t *field = (int32_t *)((char *)&g.tuning + tuning_fields[sel].offset);
                int32_t delta = tuning_fields[sel].is_fx ? 0x400 : 1;
                if (key == SDLK_ESCAPE) running = 0;
                else if (key == SDLK_F11 || (key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)))
                    SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                else if (key == SDLK_F1) debug = !debug;
                else if (key == SDLK_F2) tuning_open = !tuning_open;
                else if (key == SDLK_P) frozen = !frozen;
                else if (key == SDLK_N && frozen) step = 1;
                else if (key == SDLK_BACKSPACE) restart = 1;
                else if (key == SDLK_TAB && level_count > 1) {
                    int li = 0;
                    while (li < level_count && strcmp(level_names[li], cur_level))
                        li++;
                    snprintf(cur_level, sizeof cur_level, "%s", level_names[(li + 1) % level_count]);
                    if (load_level(cur_level) == 0)
                        restart = 1;
                } else if (tuning_open && key == SDLK_LEFTBRACKET) {
                    sel = (sel + tuning_field_count - 1) % tuning_field_count;
                } else if (tuning_open && key == SDLK_RIGHTBRACKET) {
                    sel = (sel + 1) % tuning_field_count;
                } else if (tuning_open && key == SDLK_MINUS) {
                    *field -= big ? delta * 16 : delta;
                } else if (tuning_open && key == SDLK_EQUALS) {
                    *field += big ? delta * 16 : delta;
                } else if (tuning_open && key == SDLK_F5) {
                    write_tuning_header(&g.tuning);
                } else if (tuning_open && key == SDLK_F9) {
                    tuning_defaults(&g.tuning);
                }
            }
        }
        if (restart) {
            PlayerTuning t = g.tuning;
            game_init(&g, &level, &t, abilities);
            if (record_path) {
                replay_free(&rec);
                replay_init(&rec, cur_level, g.abilities, &t);
            }
            playing = 0;
        }

        /* Fixed 60 Hz simulation, independent of the display refresh. */
        Uint64 now = SDL_GetTicksNS();
        if (now - next > tick_ns * 10)
            next = now;
        while (now >= next) {
            next += tick_ns;
            if (frozen && !step)
                continue;
            step = 0;
            InputState in[TEAM_COUNT];
            poll_input(in);
            if (playing) {
                if (playback_pos < playback.count) {
                    in[0] = playback.frames[playback_pos].in[0];
                    in[1] = playback.frames[playback_pos].in[1];
                    playback_pos++;
                } else {
                    playing = 0;
                    printf("replay done: frame %lu hash %08lx\n", (unsigned long)g.frame,
                           (unsigned long)game_hash(&g));
                }
            }
            if (record_path)
                replay_push(&rec, &in[0], &in[1]);
            game_tick(&g, in);
            if (!mute)
                sound_update(g.sfx, audio_music(&g));
            frames++;
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        scene_draw_sky(&g, &draw_api);
        scene_draw_background(&g, &draw_api);
        scene_draw(&g, &draw_api, debug);
        if (debug)
            render_debug_text(&g);
        if (tuning_open)
            render_tuning(&g.tuning, sel);
        if (frozen)
            text_bg(VIEW_W / 2 - 3 * TEXT_CW, VIEW_H - 16, "FROZEN");
        if (shot_path && max_frames >= 0 && frames >= max_frames) {
            SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
            if (!s || !SDL_SaveBMP(s, shot_path))
                fprintf(stderr, "screenshot: %s\n", SDL_GetError());
            SDL_DestroySurface(s);
            shot_path = NULL;
        }
        SDL_RenderPresent(ren);
    }

    if (record_path) {
        rec.has_hash = rec.has_kos = 1;
        rec.hash = game_hash(&g);
        rec.kos[0] = g.kos[0];
        rec.kos[1] = g.kos[1];
        if (replay_save(&rec, record_path) == 0)
            printf("wrote %s (%lu frames, hash %08lx)\n", record_path,
                   (unsigned long)rec.count, (unsigned long)rec.hash);
    }
    if (!mute)
        sound_close();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
