// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A return bound spelled only on the definition applies to later calls: the
// result holds one element, so storing it in a two-element target traps.

int *mk(int *p, int n);

int *[[cccc::array, cccc::count(n)]] mk(int *p, int n) {
    (void)n;
    return p;
}

int main(void) {
    int                                  buf[2] = {1, 2};
    int *[[cccc::array, cccc::count(2)]] q      = mk(buf, 1);
    (void)q;
    return 42;
}
