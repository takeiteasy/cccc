// CCCC_FLAGS: --testing --test-timeout=1
// CCCC_EXPECT_STDERR: ^ok 2 - code3
// An exit_code= test still runs after an earlier test timed out.
[[cccc::test]]
void hangs(void) {
    volatile int spin = 1;
    while (spin) {
    }
}

[[cccc::test(exit_code = 3)]]
int code3(void) {
    return 3;
}
