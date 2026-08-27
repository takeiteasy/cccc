// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// The restrict-value cache is gone with the bytecode optimiser, so every
// deref re-emits its CHKP3. This verifies a UAF through a restrict pointer
// is caught even when the free happens inside a called function through a
// non-restrict alias parameter: g(*p, p) frees p via its second parameter,
// and the later *p read must trap.
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
    int b = *p;       // must be caught as a UAF, not served from the cache
    return a + b;
}

int main(void) {
    int *p = malloc(sizeof(int));
    *p     = 5;
    int r  = touch(p);
    return r;
}
