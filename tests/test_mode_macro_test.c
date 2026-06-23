// CCCC_FLAGS: --testing
// Tests that __CCCC_TEST_MODE__ is defined in testing mode,
// and that __CCCC_COMP_MODE__ / __CCCC_BUILD_MODE__ are not.

[[cccc::test]]
void test_mode_macro(void) {
#ifndef __CCCC_TEST_MODE__
    AssertTrue(0);
#endif
#ifdef __CCCC_COMP_MODE__
    AssertTrue(0);
#endif
#ifdef __CCCC_BUILD_MODE__
    AssertTrue(0);
#endif
    AssertTrue(1);
}
