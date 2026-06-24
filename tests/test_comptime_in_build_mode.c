// CCCC_FLAGS: --build
// comptime macros work inside [[cccc::build]] functions.

[[cccc::comptime]]
int ct_add(int a, int b) {
    return a + b;
}

[[cccc::comptime]]
Node *ct_twenty_plus_twenty_two(void) {
    return MakeIntLiteral(ct_add(20, 22));
}

[[cccc::build]]
int build_main(void) {
    int result = ct_twenty_plus_twenty_two();
    return result == 42 ? 42 : 1;
}
