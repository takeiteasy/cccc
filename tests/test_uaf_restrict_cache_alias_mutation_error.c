// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3 --optimize=3
// CHKP3 counterpart to test_restrict_cache_argument_fill_invalidation.c's
// CHKT3-adjacent value-divergence case, and to
// test_type_check_restrict_cache_hit_error.c: demonstrates the restrict
// cache's hit-site safety check (#750) catching a genuine UAF that survives
// even with #754's invalidation fix in place.
//
// g()'s call is itself what fills the cache for *p (evaluating g(*p, p)'s
// first argument is p's first access), and g() frees p through its second,
// non-restrict parameter -- an alias to the same object. #754's post-call
// invalidate does cover this specific shape already (the call to g()
// invalidates after it returns), but keeping this test alongside the
// hit-site tests documents that the hit-site CHKP3 is a second, independent
// line of defence: it re-derives the real address and checks it against
// live heap state on every hit, so it would still catch this even if a
// future invalidation-bookkeeping change reintroduced a gap like #754's.
//
// Not a tail call (main calls touch via `int r = touch(p); return r;`,
// not `return touch(p);`): a tail call combined with a nested call before
// the UAF access hits an unrelated host-crash bug (#756) that would mask
// the safety check this test is for.
#include <stdlib.h>

static int g(int v, int *q) {
    free(q); // frees p's allocation via an aliasing, non-restrict parameter
    return v;
}

static int touch(int *restrict p) {
    int a = g(*p, p); // arg-fill: *p is read while preparing this call
    int b = *p;        // must be caught as a UAF, not served from the cache
    return a + b;
}

int main(void) {
    int *p = malloc(sizeof(int));
    *p = 5;
    int r = touch(p);
    return r;
}
