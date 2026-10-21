// An opaque FTS handle round-trips under -c=native.
#include <fts.h>
#include <stddef.h>
int main(void) {
    char *paths[2] = {".", NULL};
    FTS  *fts      = fts_open(paths, 16 | 4, NULL);
    if (!fts)
        return 1;
    FTSENT *e;
    while ((e = fts_read(fts)) != NULL) {
        (void)e;
    }
    fts_close(fts);
    return 42;
}
