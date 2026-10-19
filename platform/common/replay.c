#include "replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Button characters and their InputState bits, in output order. */
static const struct { char c; uint16_t bit; } BUTTONS[] = {
    {'L', BTN_LEFT}, {'R', BTN_RIGHT}, {'U', BTN_UP}, {'D', BTN_DOWN}, {'J', BTN_JUMP},
    {'X', BTN_DASH}, {'A', BTN_ATTACK}, {'S', BTN_START}, {'C', BTN_CONNECTED},
};
#define BUTTON_COUNT (int)(sizeof BUTTONS / sizeof BUTTONS[0])

void replay_init(Replay *r, const char *level, uint8_t abilities, const PlayerTuning *t)
{
    memset(r, 0, sizeof *r);
    snprintf(r->level, sizeof r->level, "%s", level);
    r->abilities = abilities;
    r->tuning = *t;
}

void replay_push(Replay *r, const InputState *p1, const InputState *p2)
{
    if (r->count == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 4096;
        r->frames = realloc(r->frames, r->cap * sizeof *r->frames);
        if (!r->frames) {
            perror("realloc");
            exit(1);
        }
    }
    ReplayFrame *f = &r->frames[r->count++];
    f->in[0] = *p1;
    f->in[1] = p2 ? *p2 : (InputState){0, 0, 0};
}

void replay_free(Replay *r)
{
    free(r->frames);
    memset(r, 0, sizeof *r);
}

static int32_t *tuning_field(PlayerTuning *t, int i)
{
    return (int32_t *)((char *)t + tuning_fields[i].offset);
}

/* Parse "BUTTONS [sx sy]"; returns 0 on success, else the bad character. */
static int parse_input(const char *s, InputState *in)
{
    char a[32];
    int sx = 0, sy = 0;
    *in = (InputState){0, 0, 0};
    if (sscanf(s, "%31s %d %d", a, &sx, &sy) < 1)
        return '?';
    for (const char *c = a; *c && *c != '-'; c++) {
        int i;
        for (i = 0; i < BUTTON_COUNT && BUTTONS[i].c != *c; i++)
            ;
        if (i == BUTTON_COUNT)
            return *c;
        in->buttons |= BUTTONS[i].bit;
    }
    in->stick_x = (int8_t)sx;
    in->stick_y = (int8_t)sy;
    return 0;
}

int replay_load(Replay *r, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 1;
    }
    PlayerTuning t;
    tuning_defaults(&t);
    replay_init(r, "", ABIL_ALL, &t);
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char a[64], b[64];
        long n, n2;
        int off = 0;
        if (line[0] == '#') {
            size_t used = strlen(r->comments);
            if (r->count == 0 && used + strlen(line) < sizeof r->comments)
                strcpy(r->comments + used, line);
            continue;
        }
        if (line[0] == '\n')
            continue;
        if (sscanf(line, "level %63s", r->level) == 1)
            continue;
        if (sscanf(line, "abilities %ld", &n) == 1) {
            r->abilities = (uint8_t)n;
            continue;
        }
        if (sscanf(line, "tune %63s %63s", a, b) == 2) {
            int i;
            for (i = 0; i < tuning_field_count; i++)
                if (!strcmp(a, tuning_fields[i].name))
                    break;
            if (i == tuning_field_count) {
                fprintf(stderr, "%s:%d: unknown tuning field %s\n", path, lineno, a);
                fclose(f);
                return 1;
            }
            *tuning_field(&r->tuning, i) = (int32_t)strtol(b, NULL, 0);
            continue;
        }
        if (sscanf(line, "hash %63s", a) == 1) {
            r->has_hash = 1;
            r->hash = (uint32_t)strtoul(a, NULL, 16);
            continue;
        }
        if (sscanf(line, "kos %ld %ld", &n, &n2) == 2) {
            r->has_kos = 1;
            r->kos[0] = (int)n;
            r->kos[1] = (int)n2;
            continue;
        }
        if (sscanf(line, "%ld %n", &n, &off) == 1 && n > 0) {
            InputState in[2] = {{0, 0, 0}, {0, 0, 0}};
            char *slash = strchr(line + off, '/');
            if (slash)
                *slash = 0;
            int bad = parse_input(line + off, &in[0]);
            if (!bad && slash)
                bad = parse_input(slash + 1, &in[1]);
            if (bad) {
                fprintf(stderr, "%s:%d: unknown button '%c'\n", path, lineno, bad);
                fclose(f);
                return 1;
            }
            while (n--)
                replay_push(r, &in[0], &in[1]);
            continue;
        }
        fprintf(stderr, "%s:%d: cannot parse: %s", path, lineno, line);
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

static void write_input(FILE *f, const InputState *in)
{
    char buttons[16];
    int k = 0;
    for (int i = 0; i < BUTTON_COUNT; i++)
        if (in->buttons & BUTTONS[i].bit)
            buttons[k++] = BUTTONS[i].c;
    if (!k)
        buttons[k++] = '-';
    buttons[k] = 0;
    if (in->stick_x || in->stick_y)
        fprintf(f, "%s %d %d", buttons, in->stick_x, in->stick_y);
    else
        fprintf(f, "%s", buttons);
}

static void write_run(FILE *f, long n, const ReplayFrame *fr)
{
    fprintf(f, "%ld ", n);
    write_input(f, &fr->in[0]);
    if (fr->in[1].buttons || fr->in[1].stick_x || fr->in[1].stick_y) {
        fputs(" / ", f);
        write_input(f, &fr->in[1]);
    }
    fputc('\n', f);
}

int replay_save(const Replay *r, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return 1;
    }
    PlayerTuning d;
    tuning_defaults(&d);
    fputs(r->comments[0] ? r->comments : "# n64-capture-the-flag replay (platform/common/replay.h)\n", f);
    fprintf(f, "level %s\n", r->level);
    fprintf(f, "abilities %u\n", r->abilities);
    for (int i = 0; i < tuning_field_count; i++) {
        int32_t v = *tuning_field((PlayerTuning *)&r->tuning, i);
        if (v != *tuning_field(&d, i))
            fprintf(f, "tune %s 0x%08lx\n", tuning_fields[i].name, (unsigned long)(uint32_t)v);
    }
    size_t i = 0;
    while (i < r->count) {
        size_t j = i + 1;
        while (j < r->count && !memcmp(&r->frames[j], &r->frames[i], sizeof r->frames[i]))
            j++;
        write_run(f, (long)(j - i), &r->frames[i]);
        i = j;
    }
    if (r->has_hash)
        fprintf(f, "hash %08lx\n", (unsigned long)r->hash);
    if (r->has_kos)
        fprintf(f, "kos %d %d\n", r->kos[0], r->kos[1]);
    return fclose(f) ? 1 : 0;
}

void replay_start(const Replay *r, Game *g, const Level *lv)
{
    game_init(g, lv, &r->tuning, r->abilities);
}
