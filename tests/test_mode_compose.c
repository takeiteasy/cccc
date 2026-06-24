// CCCC_FLAGS: --testing --build
// #608: composable --testing --build pipeline.
// Both __CCCC_TEST_MODE__ and __CCCC_BUILD_MODE__ must be defined; tests run
// first, then the build entry runs and returns 42.

[[cccc::test]]
void test_both_mode_macros_defined(void) {
#ifndef __CCCC_TEST_MODE__
    AssertTrue(0);
#endif
#ifndef __CCCC_BUILD_MODE__
    AssertTrue(0);
#endif
#ifdef __CCCC_COMP_MODE__
    AssertTrue(0);
#endif
    AssertTrue(1);
}

[[cccc::test]]
void test_basic_math(void) {
    AssertEq(6 * 7, 42);
}

[[cccc::build]]
int build_main(void) {
    // Reached only after all tests pass (or --fail-fast not set).
    return 42;
}
