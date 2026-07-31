// Regression test for #811: proves the tools/stdlib.tsv wiring for fts.h
// works when that header is included on its own, without any of the
// other dirent.h-family headers pulled in first -- the #792 bug class (a
// header declaring functions but registering none because
// tools/stdlib.tsv was never updated for it).
#include <fts.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char dir[] = "/tmp/cccc_fts_standalone_XXXXXX";
    if (!mkdtemp(dir)) return 1;

    char f1[512];
    snprintf(f1, sizeof(f1), "%s/only.txt", dir);
    FILE *fp = fopen(f1, "w");
    if (!fp) return 2;
    fputs("abc", fp);
    fclose(fp);

    char *paths[2] = { dir, NULL };
    FTS *fts = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (!fts) return 3;

    int saw = 0;
    FTSENT *e;
    while ((e = fts_read(fts)) != NULL) {
        if (e->fts_info == FTS_F && strcmp(e->fts_name, "only.txt") == 0) {
            saw = 1;
            if (e->fts_statp->st_size != 3) { fts_close(fts); return 4; }
        }
    }
    fts_close(fts);
    unlink(f1);
    rmdir(dir);

    return saw ? 42 : 5;
}
