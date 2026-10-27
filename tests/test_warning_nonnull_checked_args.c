// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull --checked-pointers
// CCCC_EXPECT_STDERR: null value passed to a parameter marked nonnull \(parameter 2\)
// CCCC_EXPECT_STDERR: argument may be null when passed to a parameter marked nonnull \(parameter 2\)
// CCCC_EXPECT_STDERR: null value returned from function declared with 'returns_nonnull'
// CCCC_EXPECT_STDERR: value may be null when returned from function declared with 'returns_nonnull'

// A checked argument or return value keeps the nonnull flow analysis.
__attribute__((nonnull(2))) int take(int n,
                                     int *[[cccc::array, cccc::count(n)]] p) {
    return n + (p != 0);
}

int *maybe(int c) {
    static int x;
    return c ? 0 : &x;
}

__attribute__((returns_nonnull)) int *[[cccc::array,
                                        cccc::count(1)]] ret_null(void) {
    int *q = 0;
    return q;
}

__attribute__((returns_nonnull)) int *[[cccc::array,
                                        cccc::count(1)]] ret_maybe(int c) {
    return maybe(c);
}

int main(int argc, char **argv) {
    (void)argv;
    int *q = 0;
    take(1, q);
    take(1, maybe(argc));
    (void)ret_maybe(argc);
    return 42;
}
