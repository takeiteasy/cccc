// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: error: unchecked pointer local variable 'p'
// Checked regions: a per-test warning override that does not name
// checked-regions leaves a violation an error.

[[cccc::checked]]
void f(void) {
    int *p = 0;
    (void)p;
}

[[cccc::test(return = 42, flags = "-Wall -Wno-unused")]]
int test_flagged(void) {
    return 42;
}
