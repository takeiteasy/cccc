// Test that [[cccc::test(return = IDENT)]] emits a -Wattributes warning
// and silently skips the assertion (ticket #350).
// CCCC_FLAGS: --testing -Wattributes
// CCCC_EXPECT_STDERR: warning: unrecognized return= operand 'GREEN'

// An identifier as the return= operand should warn; the test still passes
// because the assertion is skipped, not failed.
[[cccc::test(return = GREEN)]]
int test_unrecognized_ident(void) {
    return 42;
}
