// Test that an unrecognized __has_*-shaped preprocessor operator warns by
// name instead of silently degrading into a bogus function call (#1323).
// CCCC_FLAGS: --testing -Wcpp
// CCCC_EXPECT_STDERR: unknown preprocessor operator '__has_frobnicate_1323'; assuming 0

// The operator evaluates to 0 (not a compile error) -- the test still
// passes; the warning is the thing under test here.
#if __has_frobnicate_1323(x)
#error "should not be taken"
#endif

[[cccc::test(return = 42)]]
int test_unknown_pp_operator(void) {
    return 42;
}
