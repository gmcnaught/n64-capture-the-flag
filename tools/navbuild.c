/* Navigation graph files for the CPU players (core/bot.h): for each level
 * file, LEVEL.nav beside it, built with the default tuning.
 *
 * usage: navbuild LEVEL.lvl...
 * A .nav that is newer than this program and matches its level's cells
 * and the tuning is kept (the graph takes about 0.5 s a level, a level
 * with moving platforms several).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bot.h"

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    uint8_t *d = malloc(n > 0 ? (size_t)n : 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return d;
}

static BotNav nav;

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: navbuild LEVEL.lvl...\n");
        return 2;
    }
    struct stat self;
    if (stat(argv[0], &self))
        self.st_mtime = 0;
    PlayerTuning t;
    tuning_defaults(&t);
    for (int i = 1; i < argc; i++) {
        char out[512];
        const char *dot = strrchr(argv[i], '.');
        int stem = dot ? (int)(dot - argv[i]) : (int)strlen(argv[i]);
        snprintf(out, sizeof out, "%.*s.nav", stem, argv[i]);
        uint32_t size, nsize;
        uint8_t *data = read_file(argv[i], &size);
        Level lv;
        if (!data || level_load(&lv, data, size)) {
            fprintf(stderr, "%s: cannot read the level\n", argv[i]);
            return 1;
        }
        struct stat st;
        uint8_t *old = stat(out, &st) == 0 && st.st_mtime >= self.st_mtime ? read_file(out, &nsize) : NULL;
        int keep = old && bot_nav_read(&nav, &lv, &t, old, nsize) == 0;
        free(old);
        if (!keep) {
            bot_nav_begin(&nav, &lv, &t);
            while (!bot_nav_step(&nav, 64))
                ;
            uint32_t n = bot_nav_size(&nav);
            uint8_t *buf = malloc(n);
            bot_nav_write(&nav, buf);
            FILE *f = fopen(out, "wb");
            if (!f || fwrite(buf, 1, n, f) != n || fclose(f)) {
                perror(out);
                return 1;
            }
            free(buf);
            printf("%s: %u nodes, %u + %u moves\n", out, nav.node_count, nav.graphs[0].edge_count,
                   nav.graphs[1].edge_count);
        }
        free(data);
    }
    return 0;
}
