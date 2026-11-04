// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The store of `p[i] += m()` keeps its check when the macro may shrink the
// bound before the store.

[[cccc::comptime]]
Node *shrink(Node *unused) { return Quote("(n = 1)"); }

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    p[i] += shrink(0);
    return 0;
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    f(a, 4, 2);
    return 42;
}
