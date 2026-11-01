// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// An access after the bound it reads has shrunk keeps its check.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int x = p[i];
    n     = 1;
    return x + p[i];
}

int main(void) {
    int a[4] = {0};
    f(a, 4, 2);
    return 42;
}
