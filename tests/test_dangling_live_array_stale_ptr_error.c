// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --dangling-pointers
// A live array that covers a dead frame's escaped local must not hide a
// dereference through the stale pointer: only accesses rooted at the array
// itself skip the dangling check, so b[i] is fine and *p is still caught.
static long long *leak(void) {
    char                   pad = 1;
    _Alignas(32) long long t   = 5;
    long long             *p   = &t;
    return p + pad - 1;
}

static int use(long long *p) {
    char b[64];
    for (int i = 0; i < 64; i++)
        b[i] = (char)i;
    return (int)*p + b[1];
}

int main(void) {
    return use(leak()) == 6 ? 42 : 1;
}
