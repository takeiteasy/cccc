// CCCC_FLAGS: --testing=native
// CCCC_EXPECT_STDOUT: ok 1 - test_passes
// A passing native harness exits 0.
[[cccc::test(return = 42)]]
int test_passes(void) {
    return 42;
}
