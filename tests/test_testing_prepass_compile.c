// Verify that --testing can be combined with -c=bytecode (ticket #345).
// Tests run as a pre-pass; when all pass the bytecode is written.
// CCCC_FLAGS: --testing -c=bytecode -o /dev/null

[[cccc::test]]
void test_prepass_basic(void) {
    $assert_eq(1 + 1, 2);
}

[[cccc::test(return = 7)]]
int test_prepass_return(void) {
    return 7;
}
