// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: undefined variable 'NOPE'

// A name that is neither a macro nor a declared constant is a compile error,
// not a skipped assertion.
enum { GREEN = 1 };

[[cccc::test(return = NOPE)]]
int test_undeclared_name(void) {
    return GREEN;
}
