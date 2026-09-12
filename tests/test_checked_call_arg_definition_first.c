// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: with no separate prototype at all, the template resolves straight
// off the DEFINITION's own ->ty->params -- resolve_param_checked_bounds()
// (src/parse_core.c) needs no prototype-vs-definition distinction, only a
// Type with checked-pointer parameters, and a function definition's Type
// carries exactly the same checked_bounds_arg1/arg2 token spans a
// prototype's would.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n) {
    (void)p;
    (void)n;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, 8);
    return 42;
}
