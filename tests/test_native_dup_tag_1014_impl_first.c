// CCCC_FLAGS: tests/fixtures/dup_tag_1014_smoke_impl.c tests/fixtures/dup_tag_1014_smoke_private.c
// Two TUs completing a same-named but differently shaped struct tag build under
// -c=native.
#include "fixtures/dup_tag_1014_smoke.h"
extern int priv_use_1014_smoke(void);
int main(void) {
    DyGC1014Smoke *g = gc_open_1014_smoke();
    (void)priv_use_1014_smoke();
    return gc_val_1014_smoke(g);
}
