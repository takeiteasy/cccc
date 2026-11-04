// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The access after a loop keeps its check when the body shrank the bound
// before leaving by `break`.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    while (p[i] > 0) {
        n = 1;
        break;
    }
    return p[i];
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    f(a, 4, 2);
    return 42;
}
