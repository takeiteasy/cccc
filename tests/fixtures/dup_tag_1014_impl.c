// #1014: the header-exposed group -- includes the shared header and
// completes the tag with the shape gc_open_1014/gc_val_1014's own
// signatures use, so it must keep the plain `struct DyGC1014` spelling in
// -c=native/-m output no matter which TU is listed first on the command
// line.
#include "dup_tag_1014.h"

struct DyGC1014 { int v; };

static struct DyGC1014 g_1014 = { 42 };

DyGC1014 *gc_open_1014(void) { return &g_1014; }
int gc_val_1014(DyGC1014 *g) { return g->v; }
