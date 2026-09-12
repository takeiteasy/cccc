// CCCC_FLAGS: --checked-pointers
// #488: bounds(unknown) and a bare array/ntarray with no bounds form at
// all declare a checked type but leave nothing to enforce -- compute_
// checked_bounds() returns no lo/hi for either, so
// rewrite_checked_call_args() skips them the same way every other
// checked-pointer mechanism already does. Both calls here would trap
// under count(2)/count(8) if a check were wrongly emitted; asserting a
// clean exit is the negative-coverage proof that neither is.

void sink_unknown(int *[[cccc::array, cccc::bounds(unknown)]] p, int n);
void sink_bare(int *[[cccc::array]] p, int n);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};

    sink_unknown(small, 8);
    sink_bare(small, 8);

    return 42;
}

void sink_unknown(int *p, int n) {
    (void)p;
    (void)n;
}

void sink_bare(int *p, int n) {
    (void)p;
    (void)n;
}
