// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks --optimize=3
// Ticket #750: re-enabling the restrict-value cache under safety flags
// (instead of disabling it wholesale per #654) means restrict_cache_handle_
// deref's cache-hit path (src/codegen.c) must run its own CHKP3/CHKT3
// checks, since a hit reuses the cached S-register value without re-running
// emit_load_ex.
//
// A store through the *same* restrict pointer at the *same* offset, but at
// a different pointee type, hits the "write-through" branch of
// restrict_cache_handle_store: it re-stamps the real heap type shadow via
// the genuine emit_store_ex (correctly, to float) but only bit-normalizes
// the cached register value -- it does not invalidate the cache entry. The
// next *p is therefore a cache HIT, not a miss, so only the hit-site check
// can catch the resulting type mismatch.
#include <stdlib.h>

static int touch(int *restrict p) {
    int a       = *p;   // cache miss: fills the (p, 0) entry as "int"
    *(float *)p = 1.0f; // write-through: real store re-stamps heap as
                        // float, cache entry stays valid (not invalidated)
    int b = *p;         // cache hit: must still catch the int/float mismatch
    return a + b;
}

int main(void) {
    int *p = malloc(sizeof(int));
    *p     = 5;
    return touch(p);
}
