// #488: opt-in-by-default proof -- the exact program from
// test_checked_call_arg_count_trap.c (whose second argument LIES about
// `small`'s real extent), compiled with NO --checked-pointers. The
// caller-side rewrite is gated on CCCC_CHECKED_BOUNDS at parse time, same
// as every other checked-pointer mechanism, so this must run clean and
// exit 42, not trap -- proving the gate actually gates.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, 8);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
