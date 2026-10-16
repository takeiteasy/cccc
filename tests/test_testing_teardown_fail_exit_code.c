// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - code3
// CCCC_EXPECT_STDERR: teardown said no
// A failed Assert in a teardown hook fails an exit_code= test whose exit code
// matched.
[[cccc::test_teardown]]
void teardown(void) {
    AssertMsg(0, "teardown said no");
}

[[cccc::test(exit_code = 3)]]
int code3(void) {
    return 3;
}
