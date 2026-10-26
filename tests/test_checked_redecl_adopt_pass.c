// CCCC_FLAGS: --checked-pointers
// A plain prototype followed by an annotated definition takes the
// definition's bounds; a call that satisfies them runs.

void sink(int *p, int n);

void sink(int *[[cccc::array, cccc::count(n)]] p, int n) {
    (void)p;
    (void)n;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] s = (int[2]){1, 2};
    sink(s, 2);
    return 42;
}
