// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3 --optimize=3
// Regression test for a restrict-value-cache hole found while fixing #654.
// prepare_restrict_cache's "pattern 1" (a repeated *p deref on a restrict
// scalar param, src/codegen.c restrict_cache_handle_deref) had NO
// safety-flag mask at all before this fix: a cache hit reuses the value
// already sitting in the cache register and never re-emits the load, so
// CHKP3's UAF check -- which only runs from emit_load_ex -- is skipped
// entirely on the second access, not just CHKT3. Under -3 -O3, calling
// free(p) between two derefs of the same restrict pointer at the same
// offset must still be caught as a use-after-free.
#include <stdlib.h>

static int touch(int *restrict p) {
    int a = *p; // cache miss: normal load, establishes the cache entry
    free((void *)p);
    int b = *p; // must NOT be served from the stale cache entry
    return a + b;
}

int main(void) {
    int *p = malloc(sizeof(int));
    *p = 5;
    return touch(p);
}
