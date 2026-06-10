// Native mode test that deliberately fails — verifies not-ok TAP output.
// Run with --testing; expected exit code 1.
// CCCC_FLAGS: --testing

[[cccc::test(mode = "native")]]
void test_native_pass(void) {
    $assert_eq(1, 1);
}

[[cccc::test(mode = "native")]]
void test_native_expected_fail(void) {
    $assert_fail_msg("this test is supposed to fail");
}
