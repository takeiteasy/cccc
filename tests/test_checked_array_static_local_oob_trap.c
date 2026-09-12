// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #487: a block-scope `static` checked array resolves and enforces bounds
// exactly like an ordinary local -- verified explicitly since compute_
// checked_bounds()'s own comment (src/parse_checked.c) flags a static local
// as a declaration shape worth double-checking rather than assuming.

int main(void) {
    static int a _Checked[10];
    volatile int i = 10;
    a[i]           = 1;
    return 42;
}
