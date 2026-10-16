// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^not ok 1 - code3
// CCCC_EXPECT_STDERR: setup said no
// CCCC_REJECT_STDERR: Host C crash
// A failed Assert in a setup hook fails an exit_code= test that runs first.
[[cccc::test_setup]]
void setup(void) {
    AssertMsg(0, "setup said no");
}

[[cccc::test(exit_code = 3)]]
int code3(void) {
    return 3;
}
