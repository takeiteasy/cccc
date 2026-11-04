// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A macro between two identical accesses may shrink the bound, so the second
// access keeps its check.

[[cccc::comptime]]
Node *shrink(Node *unused) { return Quote("n = 1;"); }

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i) {
    int x = p[i];
    shrink(0);
    return x + p[i];
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    f(a, 4, 2);
    return 42;
}
