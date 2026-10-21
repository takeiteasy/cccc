#include "dup_tag_1014_smoke.h"
struct DyGC1014Smoke {
    int v;
};
static struct DyGC1014Smoke g_1014_smoke = {42};
DyGC1014Smoke *gc_open_1014_smoke(void) {
    return &g_1014_smoke;
}
int gc_val_1014_smoke(DyGC1014Smoke *g) {
    return g->v;
}
