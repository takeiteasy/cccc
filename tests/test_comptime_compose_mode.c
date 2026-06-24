// CCCC_FLAGS: --testing --build
// comptime(inline) macros work in composable --testing --build mode,
// usable from both test functions and the build entry.

[[cccc::comptime]]
int ct_mul(int a, int b) {
    return a * b;
}

[[cccc::comptime(inline)]]
Node *ct_six_times_seven(void) {
    return MakeIntLiteral(ct_mul(6, 7));
}

[[cccc::comptime(inline)]]
Node *ct_three_times_fourteen(void) {
    return MakeIntLiteral(ct_mul(3, 14));
}

[[cccc::test]]
void test_comptime_in_compose(void) {
    AssertEq(ct_six_times_seven(), 42);
}

[[cccc::build]]
int build_main(void) {
    int v = ct_three_times_fourteen();
    return v == 42 ? 42 : 1;
}
