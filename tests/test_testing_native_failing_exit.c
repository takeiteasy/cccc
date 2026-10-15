// CCCC_FLAGS: --testing=native
// CCCC_EXPECT_STDERR: not ok 1 - test_fails
// A failing native harness exits non-zero.
[[cccc::test(return = 42)]]
int test_fails(void) {
    return 1;
}
