// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --dangling-pointers
// #1137: an over-aligned local's returned address is still tracked as a stack
// address, so a deref after the frame returns is caught (the aligned address
// is what gets recorded, not the unaligned slot base).
static long long *leak(void) {
    char                   pad = 1;
    _Alignas(32) long long t   = 5;
    long long             *p   = &t;
    return p + pad - 1;
}

static int use(long long *p) {
    char b[64];
    b[0] = 1;
    return (int)*p + b[0];
}

int main(void) {
    return use(leak()) == 6 ? 42 : 1;
}
