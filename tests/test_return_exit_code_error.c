// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: exit_code= and return= are mutually exclusive

[[cccc::test(exit_code = 1, return = 42)]]
int test_exit_code_with_return(void) {
    return 42;
}
