// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// The restrict-value cache is gone with the bytecode optimiser, so every
// deref re-emits the load and its CHKP3. This verifies that under -3,
// calling free(p) between two derefs of the same restrict pointer is caught
// as a use-after-free -- the shape #654's cache-hit path used to skip.
#include <stdlib.h>

static int touch(int *restrict p) {
    int a = *p; // cache miss: normal load, establishes the cache entry
    free((void *)p);
    int b = *p; // must NOT be served from the stale cache entry
    return a + b;
}

int main(void) {
    int *p = malloc(sizeof(int));
    *p     = 5;
    return touch(p);
}
