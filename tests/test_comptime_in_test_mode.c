// CCCC_FLAGS: --testing
// comptime(inline) macros work inside [[cccc::test]] functions.
// This locks in the comptime-before-testing composability.

[[cccc::comptime]]
int ct_mul(int a, int b) {
    return a * b;
}

[[cccc::comptime(inline)]]
Node *ct_answer(void) {
    return MakeIntLiteral(ct_mul(6, 7));
}

[[cccc::comptime(inline)]]
Node *ct_two(void) {
    return MakeIntLiteral(ct_mul(1, 2));
}

[[cccc::test]]
void test_comptime_inline_in_test(void) {
    AssertEq(ct_answer(), 42);
    AssertEq(ct_two(), 2);
}
