// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: caller-side verification, bounds(lo, hi) form. `p` in the
// parameter's own bounds(p, p+n) is a SELF-reference -- it must
// substitute to the hidden temp standing in for the parameter's own
// value at this call, not to a raw clone of the argument expression
// (clone_param_bounds_node()'s self_idx case, src/parse_checked.c).
// `small` genuinely satisfies only 2 elements; the call's own n=100
// claims a much wider range than small's real declared extent.

void sink(int *[[cccc::array, cccc::bounds(p, p + n)]] p, int n);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, 100);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
