// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: not ok 1 - test_enum_mismatch
// CCCC_EXPECT_STDERR: expected return value =+ 0, got 1

// An enum name is compared for real: the wrong value fails the test.
enum { RED, GREEN };

[[cccc::test(return = RED)]]
int test_enum_mismatch(void) {
    return GREEN;
}
