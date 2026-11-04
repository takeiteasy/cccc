// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// An access in a loop body keeps its check when a later iteration sees a
// bound the body shrank.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int k) {
    int s = p[i];
    for (int j = 0; j < k; j++) {
        s += p[i];
        n  = 1;
    }
    return s;
}

int main(void) {
    int a[4] = {0};
    f(a, 4, 2, 3);
    return 42;
}
