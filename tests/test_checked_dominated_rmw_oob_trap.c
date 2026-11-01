// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The one check left on a read-modify-write still traps out of bounds.

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    p[i] += 1;
    return 0;
}

int main(void) {
    int a[4] = {0};
    f(a, 4, 4);
    return 42;
}
