// CCCC_FLAGS: -3 --optimize=3
// Regression test for a gap found while fixing #754 (restrict cache not
// invalidated by intrinsic-lowered calls). The original fix hoisted a
// single restrict_cache_invalidate_all() to the top of case ND_FUNCALL, run
// before that call's own argument expressions are evaluated -- but that
// misses the case where evaluating THIS call's arguments is itself what
// fills a cache entry: e.g. g(*p) reads *p (a cache miss, filling the
// (p, 0) slot) as part of preparing the call to g, and the call to g may
// itself mutate the object p points to. An invalidate is also needed AFTER
// the call, or that fill survives past it.
//
// g() here mutates the pointee through a second, non-restrict parameter
// aliasing the same object -- CHKP3 has nothing to catch (the address is
// still live), so only the invalidate (not a dangling-pointer check) can
// catch this: the bug is a stale VALUE, not a stale/invalid ADDRESS.
static int g(int v, int *q) {
    *q = 999; // mutates the object p points to, via a different parameter
    return v;
}

static int touch(int *restrict p) {
    int a = g(*p, p); // arg-fill: *p is read (and cached) while preparing
                       // this call's own arguments
    int b = *p;        // must observe 999, not the stale cached pre-call value
    return a + b;
}

int main(void) {
    int x = 5;
    int r = touch(&x); // correct: 5 + 999 = 1004 (exit code truncates mod 256)
    return (r == 1004) ? 42 : 1;
}
