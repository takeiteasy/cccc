// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// An access after an `if` keeps its check when one arm shrank the bound.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    if (c)
        n = 1;
    return x + p[i];
}

int main(void) {
    int a[4] = {0};
    f(a, 4, 2, 1);
    return 42;
}
