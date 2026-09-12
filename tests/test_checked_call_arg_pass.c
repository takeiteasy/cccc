// CCCC_FLAGS: --checked-pointers
// #488: the positive counterpart to test_checked_call_arg_count_trap.c --
// `big` genuinely satisfies the declared count(8) the call claims, so no
// CHKAB trap fires and the call proceeds normally.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int *[[cccc::array, cccc::count(8)]] big = (int[8]){0, 1, 2, 3, 4, 5, 6, 7};
    sink(big, 8);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
