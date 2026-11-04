// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A label no goto names yet may still receive the goto a macro splices in
// after checking, so a pending macro call keeps every label a join.

[[cccc::comptime]]
Node *jump_done(Node *unused) {
    return Quote("goto done;");
}

int f(int *[[cccc::array, cccc::count(n)]] p, int n, int i, int c) {
    int x = p[i];
    if (c) {
        n = 1;
        jump_done(0);
        return 0;
    }
    x += 1;
done:
    __attribute__((unused));
    return x + p[i];
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    f(a, 4, 2, 1);
    return 42;
}
