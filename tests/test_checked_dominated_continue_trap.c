// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A loop condition keeps its check when a `continue` skipped the access that
// would otherwise cover it, after shrinking the bound.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = 0;
    do {
        if (c) {
            n = 1;
            c = 0;
            continue;
        }
        x += p[i];
    } while (p[i] < 0);
    return x;
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    f(a, 4, 2, 1);
    return 42;
}
