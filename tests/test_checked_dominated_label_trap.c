// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The access at a label keeps its check when a goto arrives there after the
// bound shrank, even though the path falling into it checked the same access.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    if (c) {
        n = 1;
        goto L;
    }
    x += 1;
L:
    return x + p[i];
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    f(a, 4, 2, 1);
    return 42;
}
