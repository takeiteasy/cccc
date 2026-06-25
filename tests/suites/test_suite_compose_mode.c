// CCCC_FLAGS: --testing --build
// Consolidated suite: composable --testing --build pipeline (ticket #608).
// Source tests: test_mode_compose, test_comptime_compose_mode.

// [from test_mode_compose.c]
// Both __CCCC_TEST_MODE__ and __CCCC_BUILD_MODE__ must be defined in compose mode.
#pragma cccc suite begin "compose_mode"

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
void test_basic_math_compose(void) {
    AssertEq(6 * 7, 42);
}

// [from test_comptime_compose_mode.c]
// Comptime macros work in composable --testing --build mode.
[[cccc::comptime]]
int ct_compose_mul(int a, int b) {
    return a * b;
}

[[cccc::comptime]]
Node *ct_compose_six_times_seven(void) {
    return MakeIntLiteral(ct_compose_mul(6, 7));
}

[[cccc::comptime]]
Node *ct_compose_three_times_fourteen(void) {
    return MakeIntLiteral(ct_compose_mul(3, 14));
}

[[cccc::test]]
void test_comptime_in_compose(void) {
    AssertEq(ct_compose_six_times_seven(), 42);
}

#pragma cccc suite end

// Build entry: runs after all tests pass. Returns 0 (suite convention).
// Also validates comptime is callable from the build entry.
[[cccc::build]]
int build_main(void) {
    int v = ct_compose_three_times_fourteen();
    return v == 42 ? 0 : 1;
}
